#include "debug_utils_api.h"

#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/temp_allocator.h>
#include <foundation/os.h>
#include <foundation/path.h>

#include "tree.inl"

tm_symbol_tree_t *trees = 0;
char **files = 0;

static char *private__string_copy(const char *str)
{
	char *result = tm_alloc(tm_allocator_api->system, strlen(str) + 1);
	strcpy(result, str);
	return result;
}

static void api__search_symbols(const char *path)
{
	const tm_file_stat_t stat = tm_os_api->file_system->stat(path);
	const size_t old_symbols_size = tm_carray_size(trees);

	if (!stat.exists) return;
	else if (stat.is_directory) {
		TM_INIT_TEMP_ALLOCATOR(ta);
		const size_t path_len = strlen(path);

		tm_strings_t *entries = tm_os_api->file_system->directory_entries(path, ta);
		char *string = (char *)entries + sizeof(tm_strings_t);
		for (uint32_t i = 0; i < entries->count; ++i, string += strlen(string) + 1) {
			if (string[0] == '.')
				continue;

			if (!strcmp(path, "."))
				api__search_symbols(string);
			else {
				const size_t string_len = strlen(string);
				char *joined = ta->realloc(ta->inst, NULL, 0, path_len + string_len + 2);
				strcpy(joined, path);
				joined[path_len] = '/';
				strcpy(joined + path_len + 1, string);

				api__search_symbols(joined);
				ta->realloc(ta->inst, joined, path_len + string_len + 2, 0);
			}
		}

		TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	}
	else {
		const char *ext = 0;
		tm_path_api->split(path, &ext);
		if (!strcmp(ext, ".hdb")) {
			size_t i = 0;
			for (; i < old_symbols_size; ++i) {
				if (!strcmp(path, files[i]))
					break;
			}

			if (i == old_symbols_size) {
				tm_carray_push(files, private__string_copy(path), tm_allocator_api->system);

				tm_file_o file = tm_os_api->file_io->open_input(path);

				tm_symbol_tree_t tree;
				tm_os_api->file_io->read(file, &tree.node_count, sizeof(uint32_t));

				tree.nodes = tm_alloc(tm_allocator_api->system, tree.node_count * sizeof(tm_symbol_node_t));
				tm_os_api->file_io->read(file, tree.nodes, tree.node_count * sizeof(tm_symbol_node_t));
				tm_carray_push(trees, tree, tm_allocator_api->system);
				tm_os_api->file_io->close(file);
			}
		}
	}
}

static const char *api__decode_hash(uint64_t hash, tm_temp_allocator_i *ta)
{
	if (!trees)
		api__search_symbols("../../");

	const size_t db_size = tm_carray_size(trees);
	for (size_t i = 0; i < db_size; ++i) {

		uint32_t node_idx;
		if (tm_symbol_tree_search(trees + i, hash, &node_idx))
		{
			const tm_symbol_node_t *node = trees[i].nodes + node_idx;
			char *buffer = tm_temp_alloc(ta, node->string_length + 1ull);

			tm_file_o file = tm_os_api->file_io->open_input(files[i]);
			tm_os_api->file_io->read_at(file, node->byte_offset, buffer, node->string_length);
			tm_os_api->file_io->close(file);

			buffer[node->string_length] = '\0';
			return buffer;
		}
	}

	return 0;
}

static const char *api__try_decode_hash(uint64_t hash, tm_temp_allocator_i *ta)
{
	const char *result = api__decode_hash(hash, ta);
	if (!result) result = tm_temp_allocator_api->printf(ta, "%llx", hash);
	return result;
}

struct tm_debug_utils_api *tm_debug_utils_api = &(struct tm_debug_utils_api)
{
	.add_symbols = api__search_symbols,
	.decode_hash = api__decode_hash,
	.try_decode_hash = api__try_decode_hash
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
	tm_set_or_remove_api(reg, load, TM_DEBUG_UTILS_API_NAME, tm_debug_utils_api);

	if (!load) {
		const size_t db_size = tm_carray_size(trees);
		for (size_t i = 0; i < db_size; ++i) {
			tm_free(tm_allocator_api->system, files[i], strlen(files[i]) + 1);
			tm_symbol_tree_free(tm_allocator_api->system, trees + i);
		}

		tm_carray_free(files, tm_allocator_api->system);
		tm_carray_free(trees, tm_allocator_api->system);
	}
}