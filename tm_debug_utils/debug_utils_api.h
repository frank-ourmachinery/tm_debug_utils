#pragma once
#include <foundation/api_types.h>

#define TM_DEBUG_UTILS_API_NAME "tm_debug_utils"

struct tm_temp_allocator_i;

struct tm_debug_utils_api
{
	// Reverses the specified hash into the string that generated it.
	// Result is allocated with the specified allocator or null if the hash was not found.
	const char *(*decode_hash)(uint64_t hash, struct tm_temp_allocator_i *ta);
	// Attempts to reverse the specified hash into the string that genrated it.
	// Returns whether this was successful, result is allocated though the specified temporary allocator.
	bool (*try_decode_hash)(uint64_t hash, const char **result, struct tm_temp_allocator_i *ta);
	// Searches the specified path and sub directories for The Machinery symbols.
	void (*add_symbols)(const char *path);
};

#if defined(TM_LINKS_DEBUG_UTILS)
extern struct tm_debug_utils_api *tm_debug_utils_api;
#endif