#define TM_FILE_SEARCH_BLOCK_SIZE (1 << 12)

static void tm_symbols_save(tm_symbol_tree_t *tree, const char **strings, const char *path)
{
	TM_INIT_TEMP_ALLOCATOR(ta);

	const char *path_with_extension = tm_temp_allocator_api->printf(ta, "%s.hdb", path);
	tm_file_o file = tm_os_api->file_io->open_output(path_with_extension, false);

	tm_os_api->file_io->write(file, &tree->node_count, sizeof(uint32_t));
	tm_os_api->file_io->write(file, tree->nodes, tree->node_count * sizeof(tm_symbol_node_t));

	const size_t size = tm_carray_size(strings);
	for (size_t i = 0; i < size; ++i)
		tm_os_api->file_io->write(file, strings[i], strlen(strings[i]));

	tm_os_api->file_io->close(file);
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static void tm_symbols_tree_finalize(tm_symbol_tree_t *tree)
{
	const uint64_t adder = sizeof(uint32_t) + tree->node_count * sizeof(tm_symbol_node_t);
	for (tm_symbol_node_t *node = tree->nodes; node != tree->nodes + tree->node_count; ++node)
		node->byte_offset += adder;
}

static void tm_symbols_add_entry(tm_allocator_i *a, tm_symbol_tree_t *tree, const char ***string_buffer, uint64_t *offset, const char *string)
{
	const uint64_t hash = tm_murmur_hash_string_inline(string);
	if (!tm_symbol_tree_contains(tree, hash)) {
		const uint32_t string_length = (uint32_t)strlen(string);
		tm_symbol_tree_insert(a, tree, hash, *offset, string_length);

		char *string_copy = tm_alloc(a, string_length + 1);
		strcpy(string_copy, string);
		tm_carray_push(*string_buffer, string_copy, a);

		printf_loud("%s\n", string);
		*offset += string_length;
	}
}

static void tm_symbols_search_file(tm_allocator_i *a, tm_symbol_tree_t *tree, const char ***string_buffer, uint64_t *offset, const char *path)
{
	tm_file_o file = tm_os_api->file_io->open_input(path);
	tm_symbols_add_entry(a, tree, string_buffer, offset, tm_path_api->split(path, NULL));
	printf_loud("----------------------------%s----------------------------\n", path);

	uint64_t memory[TM_FILE_SEARCH_BLOCK_SIZE / sizeof(uint64_t)];
	char *buffer = (char *)memory;

	bool string_has_started = false;
	int in_comment = false;
	int64_t bytes_read;
	size_t bytes_carried = 0;
	while ((bytes_read = tm_os_api->file_io->read(file, buffer + bytes_carried, TM_FILE_SEARCH_BLOCK_SIZE - bytes_carried)) > 0) {
		const size_t chunk_length = bytes_carried + bytes_read;
		size_t string_start = 0;

		for (size_t i = 0; i < chunk_length; ++i) {
			// Skip anything in a comment.
			if (in_comment) {
				if ((in_comment == 1 && buffer[i] == '\n') || (in_comment == 2 && buffer[i] == '*' && buffer[i + 1] == '/'))
					in_comment = false;

				continue;
			} else if (!string_has_started && buffer[i] == '/' && (buffer[i + 1] == '/' || buffer[i + 1] == '*')) {
				in_comment = 1 + (buffer[i + 1] == '*');
				continue;
			}

			if (buffer[i] != '"')
				continue;

			// Skip any " characters that are in a literal char ('"') or are used within a literal string (\").
			if (buffer[i] == '"' && ((buffer[i - 1] == '\\' && buffer[i - 2] != '\\') || (buffer[i - 1] == '\'' && buffer[i + 1] == '\'')))
				continue;

			if (string_has_started) {
				buffer[i] = '\0';
				string_has_started = false;
				tm_symbols_add_entry(a, tree, string_buffer, offset, buffer + string_start);
			} else {
				string_has_started = true;
				string_start = i + 1;
			}
		}

		if (string_has_started) {
			bytes_carried = max(1, chunk_length - string_start);
			memcpy(buffer, buffer + string_start, bytes_carried);
		}
	}

	tm_os_api->file_io->close(file);
}

static void tm_symbols_search_file_or_dir(tm_allocator_i *a, tm_symbol_tree_t *tree, const char ***string_buffer, uint64_t *offset, const char *path)
{
	tm_file_stat_t stat = tm_os_api->file_system->stat(path);
	if (!stat.exists) 
		return;
	else if (stat.is_directory) {
		TM_INIT_TEMP_ALLOCATOR(ta);
		tm_strings_t *entries = tm_os_api->file_system->directory_entries(path, ta);
		char *s = (char *)entries + sizeof(tm_strings_t);
		for (uint32_t i = 0; i < entries->count; ++i) {
			char *cur = s;
			s += strlen(s) + 1;

			if (cur[0] == '.') continue;
			if (!strcmp(path, "."))
				tm_symbols_search_file_or_dir(a, tree, string_buffer, offset, cur);
			else {
				char *joined = tm_temp_allocator_api->printf(ta, "%s/%s", path, cur);
				tm_symbols_search_file_or_dir(a, tree, string_buffer, offset, joined);
				ta->realloc(ta->inst, joined, strlen(joined + 1), 0);
			}
		}

		TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	}
	else {
		const char *ext = 0;
		tm_path_api->split(path, &ext);

		const char *valid_extensions[] = { ".c", ".cpp", ".h", ".hpp", ".inl", ".inc" };
		for (size_t i = 0; i < TM_ARRAY_COUNT(valid_extensions); ++i) {
			if (!strcmp(ext, valid_extensions[i])) {
				tm_symbols_search_file(a, tree, string_buffer, offset, path);
				break;
			}
		}
	}
}

static void tm_symbols_search_and_save(tm_allocator_i *a, const char *input_path, const char *output_path)
{
	tm_symbol_tree_t tree = { 0 };
	uint64_t offset = 0;
	char **string_buffer = 0;

	tm_symbols_search_file_or_dir(a, &tree, &string_buffer, &offset, input_path);
	tm_symbols_tree_finalize(&tree);
	tm_symbols_save(&tree, string_buffer, output_path);

	tm_symbol_tree_free(tm_allocator_api->system, &tree);
	for (size_t i = 0; i < tm_carray_size(string_buffer); ++i)
		tm_free(a, string_buffer[i], strlen(string_buffer[i]) + 1);
	tm_carray_free(string_buffer, a);
}