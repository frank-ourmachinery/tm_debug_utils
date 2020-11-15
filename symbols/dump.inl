static void tm_symbol_dump_node_to_user(const tm_symbol_tree_t *tree, uint32_t root_idx, tm_file_o file, uint32_t *page_count)
{
	const tm_symbol_node_t *node = tree->nodes + root_idx;
	
	if (node->left)
		tm_symbol_dump_node_to_user(tree, node->left, file, page_count);

	if (page_threshold && (*page_count)++ == page_threshold) {
		*page_count = 0;
		tm_logger_api->print(TM_LOG_TYPE_INFO, "press any key to load more...");
		getchar();
	}

	TM_INIT_TEMP_ALLOCATOR(ta);
	char *buffer = tm_temp_alloc(ta, node->string_length + 1ull);
	tm_os_api->file_io->read_at(file, node->byte_offset, buffer, node->string_length);
	buffer[node->string_length] = '\0';
	tm_logger_api->printf(TM_LOG_TYPE_INFO, "[0x%llx] \"%s\"\n", node->hash, buffer);
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	if (node->right)
		tm_symbol_dump_node_to_user(tree, node->right, file, page_count);
}

static void tm_symbol_dump_node_to_file(const tm_symbol_tree_t *tree, uint32_t root_idx, tm_file_o input_file, tm_file_o output_file)
{
	const tm_symbol_node_t *node = tree->nodes + root_idx;

	if (node->left)
		tm_symbol_dump_node_to_file(tree, node->left, input_file, output_file);

	TM_INIT_TEMP_ALLOCATOR(ta);
	char *buffer = tm_temp_alloc(ta, node->string_length + 1ull);
	tm_os_api->file_io->read_at(input_file, node->byte_offset, buffer, node->string_length);
	buffer[node->string_length] = '\0';

	const char *line = tm_temp_allocator_api->printf(ta, "[0x%llx] \"%s\"\n", node->hash, buffer);
	tm_os_api->file_io->write(output_file, line, strlen(line));
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	if (node->right)
		tm_symbol_dump_node_to_file(tree, node->right, input_file, output_file);
}

static void tm_symbols_dump_file_to_user(tm_allocator_i *a, const char *input)
{
	tm_file_o file = tm_os_api->file_io->open_input(input);
	uint32_t page_count = 0;

	tm_symbol_tree_t tree;
	tm_os_api->file_io->read(file, &tree.node_count, sizeof(uint32_t));
	tree.nodes = tm_alloc(a, tree.node_count * sizeof(tm_symbol_node_t));
	tm_os_api->file_io->read(file, tree.nodes, tree.node_count * sizeof(tm_symbol_node_t));

	tm_symbol_dump_node_to_user(&tree, 0, file, &page_count);

	tm_symbol_tree_free(a, &tree);
	tm_os_api->file_io->close(file);
}

static void tm_symbols_dump_file_to_file(tm_allocator_i *a, const char *input, const char *output)
{
	tm_file_o input_file = tm_os_api->file_io->open_input(input);
	tm_file_o output_file = tm_os_api->file_io->open_output(output, true);

	tm_symbol_tree_t tree;
	tm_os_api->file_io->read(input_file, &tree.node_count, sizeof(uint32_t));
	tree.nodes = tm_alloc(a, tree.node_count * sizeof(tm_symbol_node_t));
	tm_os_api->file_io->read(input_file, tree.nodes, tree.node_count * sizeof(tm_symbol_node_t));

	tm_symbol_dump_node_to_file(&tree, 0, input_file, output_file);

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