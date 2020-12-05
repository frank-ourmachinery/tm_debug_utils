static const char *tm_symbol_dump_decode(const tm_huffman_tree_t *tree, const tm_symbol_node_t *node, tm_file_o file, tm_temp_allocator_i *ta)
{
	char *buffer;
	uint64_t string_length;

	if (tree) {
		const uint64_t encoded_end = (node->string_start & 7) + node->string_length;
		const uint64_t encoded_length = (node->string_length >> 3) + 1ull;

		char *code_buffer = tm_temp_alloc(ta, encoded_length);
		tm_os_api->file_io->read_at(file, node->string_start >> 3, code_buffer, encoded_length);

		uint64_t offset = node->string_start & 7;
		buffer = tm_temp_alloc(ta, encoded_length);
		for (string_length = 0; offset < encoded_end; ++string_length)
			buffer[string_length] = tm_huffman_tree_decode(tree, code_buffer, &offset);

		ta->realloc(ta->inst, code_buffer, encoded_length, 0);
		ta->realloc(ta->inst, buffer, encoded_length, string_length);
	} else {
		string_length = node->string_length;
		buffer = tm_temp_alloc(ta, string_length + 1);
		tm_os_api->file_io->read_at(file, node->string_start, buffer, string_length);
	}

	buffer[string_length] = '\0';
	return buffer;
}

static void tm_symbol_dump_node_to_user(const tm_symbol_tree_t *tree, const tm_huffman_tree_t *decoding, uint32_t root_idx, tm_file_o file, uint32_t *page_count)
{
	const tm_symbol_node_t *node = tree->nodes + root_idx;
	
	if (node->left)
		tm_symbol_dump_node_to_user(tree, decoding, node->left, file, page_count);

	if (page_threshold && (*page_count)++ == page_threshold) {
		*page_count = 0;
		tm_logger_api->print(TM_LOG_TYPE_INFO, "press any key to load more...");
		getchar();
	}

	TM_INIT_TEMP_ALLOCATOR(ta);
	const char *string = tm_symbol_dump_decode(decoding, node, file, ta);
	tm_logger_api->printf(TM_LOG_TYPE_INFO, "[0x%llx] \"%s\"\n", node->hash, string);
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	if (node->right)
		tm_symbol_dump_node_to_user(tree, decoding, node->right, file, page_count);
}

static void tm_symbol_dump_node_to_file(const tm_symbol_tree_t *tree, const tm_huffman_tree_t *decoding, uint32_t root_idx, tm_file_o input_file, tm_file_o output_file)
{
	const tm_symbol_node_t *node = tree->nodes + root_idx;

	if (node->left)
		tm_symbol_dump_node_to_file(tree, decoding, node->left, input_file, output_file);

	TM_INIT_TEMP_ALLOCATOR(ta);
	const char *string = tm_symbol_dump_decode(decoding, node, input_file, ta);
	const char *line = tm_temp_allocator_api->printf(ta, "[0x%llx] \"%s\"\n", node->hash, string);
	tm_os_api->file_io->write(output_file, line, strlen(line));
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	if (node->right)
		tm_symbol_dump_node_to_file(tree, decoding, node->right, input_file, output_file);
}

static void tm_symbols_dump_file_to_user(tm_allocator_i *a, const char *input)
{
	tm_file_o file = tm_os_api->file_io->open_input(input);
	uint32_t page_count = 0;

	uint32_t flags;
	tm_symbol_tree_t tree = { 0 };
	tm_os_api->file_io->read(file, &flags, sizeof(uint32_t));
	tm_os_api->file_io->read(file, &tree.node_count, sizeof(uint32_t));
	tree.nodes = tm_alloc(a, tree.node_count * sizeof(tm_symbol_node_t));
	tm_os_api->file_io->read(file, tree.nodes, tree.node_count * sizeof(tm_symbol_node_t));

	if (flags & TM_HDB_FLAGS_COMPRESSED) {
		tm_huffman_tree_t decoding = { 0 };
		tm_os_api->file_io->read(file, &decoding.node_count, sizeof(uint32_t));
		decoding.nodes = tm_alloc(a, decoding.node_count * sizeof(tm_huffman_node_t));
		tm_os_api->file_io->read(file, decoding.nodes, decoding.node_count * sizeof(tm_huffman_node_t));

		tm_symbol_dump_node_to_user(&tree, &decoding, 0, file, &page_count);
		tm_huffman_tree_free(a, &decoding);
	} else
		tm_symbol_dump_node_to_user(&tree, 0, 0, file, &page_count);

	tm_symbol_tree_free(a, &tree);
	tm_os_api->file_io->close(file);
}

static void tm_symbols_dump_file_to_file(tm_allocator_i *a, const char *input, const char *output)
{
	tm_file_o input_file = tm_os_api->file_io->open_input(input);
	tm_file_o output_file = tm_os_api->file_io->open_output(output, true);

	uint32_t flags;
	tm_symbol_tree_t tree = { 0 };
	tm_os_api->file_io->read(input_file, &flags, sizeof(uint32_t));
	tm_os_api->file_io->read(input_file, &tree.node_count, sizeof(uint32_t));
	tree.nodes = tm_alloc(a, tree.node_count * sizeof(tm_symbol_node_t));
	tm_os_api->file_io->read(input_file, tree.nodes, tree.node_count * sizeof(tm_symbol_node_t));

	if (flags & TM_HDB_FLAGS_COMPRESSED) {
		tm_huffman_tree_t decoding = { 0 };
		tm_os_api->file_io->read(input_file, &decoding.node_count, sizeof(uint32_t));
		decoding.nodes = tm_alloc(a, decoding.node_count * sizeof(tm_huffman_node_t));
		tm_os_api->file_io->read(input_file, decoding.nodes, decoding.node_count * sizeof(tm_huffman_node_t));

		tm_symbol_dump_node_to_file(&tree, &decoding, 0, input_file, output_file);
		tm_huffman_tree_free(a, &decoding);
	} else
		tm_symbol_dump_node_to_file(&tree, 0, 0, input_file, output_file);

	tm_symbol_tree_free(a, &tree);
	tm_os_api->file_io->close(input_file);
	tm_os_api->file_io->close(output_file);
}

static void tm_symbols_dump_file_or_dir_to_user(tm_allocator_i *a, const char *input)
{
	tm_file_stat_t stat = tm_os_api->file_system->stat(input);
	if (!stat.exists)
		return;
	else if (stat.is_directory) {
		TM_INIT_TEMP_ALLOCATOR(ta);
		tm_strings_t *entries = tm_os_api->file_system->directory_entries(input, ta);
		char *s = (char *)entries + sizeof(tm_strings_t);
		for (uint32_t i = 0; i < entries->count; ++i) {
			char *cur = s;
			s += strlen(s) + 1;

			if (cur[0] == '.') continue;
			if (!strcmp(input, "."))
				tm_symbols_dump_file_or_dir_to_user(a, cur);
			else {
				char *joined = tm_temp_allocator_api->printf(ta, "%s/%s", input, cur);
				tm_symbols_dump_file_or_dir_to_user(a, joined);
				ta->realloc(ta->inst, joined, strlen(joined + 1), 0);
			}
		}

		TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	}
	else {
		const char *ext = 0;
		tm_path_api->split(input, &ext);
		if (!strcmp(ext, ".hdb"))
			tm_symbols_dump_file_to_user(a, input);
	}
}

static void tm_symbols_dump_file_or_dir_to_file(tm_allocator_i *a, const char *input, const char *output)
{
	tm_file_stat_t stat = tm_os_api->file_system->stat(input);
	if (!stat.exists)
		return;
	else if (stat.is_directory) {
		TM_INIT_TEMP_ALLOCATOR(ta);
		tm_strings_t *entries = tm_os_api->file_system->directory_entries(input, ta);
		char *s = (char *)entries + sizeof(tm_strings_t);
		for (uint32_t i = 0; i < entries->count; ++i) {
			char *cur = s;
			s += strlen(s) + 1;

			if (cur[0] == '.') continue;
			if (!strcmp(input, "."))
				tm_symbols_dump_file_or_dir_to_file(a, cur, output);
			else {
				char *joined = tm_temp_allocator_api->printf(ta, "%s/%s", input, cur);
				tm_symbols_dump_file_or_dir_to_file(a, joined, output);
				ta->realloc(ta->inst, joined, strlen(joined + 1), 0);
			}
		}

		TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	}
	else {
		const char *ext = 0;
		tm_path_api->split(input, &ext);
		if (!strcmp(ext, ".hdb"))
			tm_symbols_dump_file_to_file(a, input, output);
	}
}