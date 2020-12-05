#include "debug_utils_api.h"

#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/log.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/temp_allocator.h>
#include <foundation/os.h>
#include <foundation/path.h>

#include "tree.inl"
#include "binary_handler.inl"
#include "huffman.inl"

static tm_symbol_tree_t runtime_tree;
static char *runtime_strings = 0;
static size_t runtime_buffer_size = 0;

static tm_symbol_tree_t *trees = 0;
static tm_huffman_tree_t *decoding = 0;
static char **files = 0;

#define allocator tm_allocator_api->system

static char *private__string_copy(const char *str)
{
	char *result = tm_alloc(allocator, strlen(str) + 1);
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
				tm_file_o file = tm_os_api->file_io->open_input(path);

				uint32_t flags;
				tm_os_api->file_io->read(file, &flags, sizeof(uint32_t));
				if ((flags & TM_HDB_FLAGS_VERSION_MASK) == TM_HDB_FLAGS_VERSION) {

					tm_carray_push(files, private__string_copy(path), allocator);

					tm_symbol_tree_t tree;
					tm_os_api->file_io->read(file, &tree.node_count, sizeof(uint32_t));

					tree.nodes = tm_alloc(allocator, tree.node_count * sizeof(tm_symbol_node_t));
					tm_os_api->file_io->read(file, tree.nodes, tree.node_count * sizeof(tm_symbol_node_t));
					tm_carray_push(trees, tree, allocator);

					if (flags & TM_HDB_FLAGS_COMPRESSED) {
						tm_huffman_tree_t huffman;
						tm_os_api->file_io->read(file, &huffman.node_count, sizeof(uint32_t));
						huffman.nodes = tm_alloc(allocator, huffman.node_count * sizeof(tm_huffman_node_t));
						tm_os_api->file_io->read(file, huffman.nodes, huffman.node_count * sizeof(tm_huffman_node_t));
						tm_carray_push(decoding, huffman, allocator);
					}
					else {
						tm_carray_push(decoding, (tm_huffman_tree_t) { 0 }, allocator);
					}
				}

				tm_os_api->file_io->close(file);
			}
		}
	}
}

static const char *api__decode_hash(uint64_t hash, tm_temp_allocator_i *ta)
{
	if (!trees)
		api__search_symbols("../../");

	uint32_t node_idx;
	if (tm_symbol_tree_try_search(&runtime_tree, hash, &node_idx)) {
		const tm_symbol_node_t *node = runtime_tree.nodes + node_idx;
		char *buffer = tm_temp_alloc(ta, node->string_length + 1ull);
		memcpy(buffer, runtime_strings + node->string_start, node->string_length);
		buffer[node->string_length] = '\0';
		return buffer;
	}

	const size_t db_size = tm_carray_size(trees);
	for (size_t i = 0; i < db_size; ++i) {

		if (tm_symbol_tree_try_search(trees + i, hash, &node_idx))
		{
			tm_file_o file = tm_os_api->file_io->open_input(files[i]);
			const tm_symbol_node_t *node = trees[i].nodes + node_idx;

			char *buffer;
			uint64_t string_length;

			if (decoding[i].node_count) {
				const uint64_t encoded_end = (node->string_start & 7) + node->string_length;
				const uint64_t encoded_length = (node->string_length >> 3) + 1ull;

				char *code_buffer = tm_temp_alloc(ta, encoded_length);
				tm_os_api->file_io->read_at(file, node->string_start >> 3, code_buffer, encoded_length);

				uint64_t offset = node->string_start & 7;
				buffer = tm_temp_alloc(ta, encoded_length);
				for (string_length = 0; offset < encoded_end; ++string_length)
					buffer[string_length] = tm_huffman_tree_decode(decoding + i, code_buffer, &offset);

				ta->realloc(ta->inst, code_buffer, encoded_length, 0);
				ta->realloc(ta->inst, buffer, encoded_length, string_length);
			} else {
				string_length = node->string_length;
				buffer = tm_temp_alloc(ta, string_length + 1);
				tm_os_api->file_io->read_at(file, node->string_start, buffer, string_length);
			}

			tm_os_api->file_io->close(file);
			buffer[string_length] = '\0';
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

static uint64_t api__add_hash(const char *string)
{
	const uint64_t hash = tm_murmur_hash_string_inline(string);

	if (!tm_symbol_tree_contains(&runtime_tree, hash)) {
		const uint32_t length = (uint32_t)strlen(string);
		tm_symbol_tree_insert(allocator, &runtime_tree, hash, runtime_buffer_size, length);

		const size_t new_size = runtime_buffer_size + length;
		runtime_strings = tm_realloc(allocator, runtime_strings, runtime_buffer_size, new_size);
		memcpy(runtime_strings + runtime_buffer_size, string, length);
		runtime_buffer_size = new_size;
	}

	return hash;
}

struct tm_debug_utils_api *tm_debug_utils_api = &(struct tm_debug_utils_api)
{
	.add_symbol_database = api__search_symbols,
	.decode_hash = api__decode_hash,
	.try_decode_hash = api__try_decode_hash,
	.add_hash = api__add_hash
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
	tm_set_or_remove_api(reg, load, TM_DEBUG_UTILS_API_NAME, tm_debug_utils_api);

	if (!load) {
		const size_t db_size = tm_carray_size(trees);
		for (size_t i = 0; i < db_size; ++i) {
			tm_free(allocator, files[i], strlen(files[i]) + 1);
			tm_symbol_tree_free(allocator, trees + i);
		}

		tm_symbol_tree_free(allocator, &runtime_tree);
		tm_free(allocator, runtime_strings, runtime_buffer_size);
		runtime_strings = 0;
		runtime_buffer_size = 0;

		tm_carray_free(files, allocator);
		tm_carray_free(trees, allocator);
		tm_carray_free(decoding, allocator);
		files = 0;
		trees = 0;
		decoding = 0;
	}
}