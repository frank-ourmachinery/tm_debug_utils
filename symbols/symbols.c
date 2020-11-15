#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/log.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/os.h>
#include <foundation/path.h>
#include <foundation/temp_allocator.h>

#include "debug_utils_api.h"

#include <stdlib.h>
#include <stdio.h>

#define print_loud(msg)						if (loud) tm_logger_api->print(TM_LOG_TYPE_INFO, msg)
#define printf_loud(format, ...)			if (loud) tm_logger_api->printf(TM_LOG_TYPE_INFO, format, __VA_ARGS__)

static bool loud = true;
static uint32_t page_threshold = 0;

#include "tree.inl"
#include "generate.inl"
#include "dump.inl"

static void print_usage()
{
	tm_logger_api->print(TM_LOG_TYPE_INFO,
		"Usage dbgutils [OPTIONS]...\n"
		"Generates debugging information for The Machinery projects.\n"
		"\n"
		"	-h\n"
		"	--help\n"
		"		Displays this help messgae and exits.\n"
		"\n"
		"	-q\n"
		"	--quiet\n"
		"		Suppresses all but essential logging.\n"
		"\n"
		"	-s [STRING]\n"
		"	--search [STRING]\n"
		"		Searches the database for the hash and returns the string that generated it.\n"
		"\n"
		"	-d\n"
		"	--dump\n"
		"		Logs a human readable version of the symbol database specified (with --input) or generated (with --generate).\n"
		"\n"
		"	--page [NUMBER]\n"
		"		When dumping a file to the user, stops after every [NUMBER] entries and waits for user input.\n"
		"\n"
		"	--decimal\n"
		"		Uses a radix of 10 instead of 16 when converting --search inputs to numbers.\n"
		"\n"
		"	-g\n"
		"	--generate\n"
		"		Generates a .tmpdb file for the specified files or for all child files in the current directory.\n"
		"		This file contains The Machinery specific debugging information, like a hash lookup table.\n"
		"\n"
		"	-i [STRING]\n"
		"	--input [STRING]\n"
		"		Specifies a file or directory path to start searching from.\n"
		"\n"
		"	-o [STRING]\n"
		"	--output [STRING]\n"
		"		Specifies the output path for the .tmpdb file if --generate is active or for a dump file if --dump is active.\n"
		"\n");
}

static inline bool arg_eql(const char *arg, const char *opt_short, const char *opt_long)
{
	return !(strcmp(arg, opt_short) && strcmp(arg, opt_long));
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	SetConsoleOutputCP(65001);
#endif

	TM_INIT_TEMP_ALLOCATOR(ta);
	tm_logger_api->add_logger(tm_logger_api->printf_logger);

	bool generate = false;
	bool dump = false;
	int radix = 16;
	const char *path = tm_path_api_dir(argv[0], tm_path_api->split(argv[0], NULL), ta);
	const char *output = 0;
	const char **queries = 0;

	for (int i = 1; i < argc; ++i) {
		if (arg_eql(argv[i], "-h", "--help")) {
			print_usage();
			TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
			return EXIT_SUCCESS;
		}
		else if (arg_eql(argv[i], "-q", "--quiet")) loud = false;
		else if (arg_eql(argv[i], "-g", "--generate")) generate = true;
		else if (arg_eql(argv[i], "-d", "--dump")) dump = true;
		else if (!strcmp(argv[i], "--decimal")) radix = 10;
		else if (arg_eql(argv[i], "-i", "--input")) {
			if (i + 1 < argc) path = argv[++i];
			else {
				tm_logger_api->print(TM_LOG_TYPE_ERROR, "dbgutils: no path was specified after --input!\n");
				return EXIT_FAILURE;
			}
		}
		else if (arg_eql(argv[i], "-o", "--output")) {
			if (i + 1 < argc) output = argv[++i];
			else {
				tm_logger_api->print(TM_LOG_TYPE_ERROR, "dbgutils: no file was specified after --output!\n");
				return EXIT_FAILURE;
			}
		}
		else if (arg_eql(argv[i], "-s", "--search")) {
			if (i + 1 < argc) tm_carray_temp_push(queries, argv[++i], ta);
			else {
				tm_logger_api->print(TM_LOG_TYPE_ERROR, "dbgutils: no query was specified after --search!\n");
				return EXIT_FAILURE;
			}
		}
		else if (!strcmp(argv[i], "--page")) {
			if (i + 1 < argc) page_threshold = strtoul(argv[++i], NULL, 10);
			else {
				tm_logger_api->print(TM_LOG_TYPE_ERROR, "dbgutils: no page count was specified after --page!\n");
				return EXIT_FAILURE;
			}
		}
		else if (argv[i][0] == '-') {
			tm_logger_api->printf(TM_LOG_TYPE_ERROR,
				"dbgutils: unknown option --%s\n"
				"Try 'dbgutils --help' for available commands.\n", argv[i]);

			return EXIT_FAILURE;
		}
	}

	const tm_clock_o start_time = tm_os_api->time->now();

	if (path)
		tm_debug_utils_api->add_symbols(path);

	for (size_t i = 0; i < tm_carray_size(queries); ++i) {
		tm_logger_api->printf(TM_LOG_TYPE_INFO, "dbgutils: %s = '%s'\n", queries[i], tm_debug_utils_api->decode_hash(strtoull(queries[i], NULL, radix), ta));
	}

	if (generate) {
		if (!output) output = tm_temp_allocator_api->printf(ta, "%s/%s", tm_path_api_dir(argv[0], tm_path_api->split(argv[0], NULL), ta), tm_path_api->split(path, NULL));
		tm_symbols_search_and_save(tm_allocator_api->system, path, output);
	}

	if (dump) {
		const char *output_dir = tm_path_api_dir(argv[0], tm_path_api->split(argv[0], NULL), ta);
		const char *input_file = generate ? output_dir : path;

		if (output) {
			const char *output_file = tm_temp_allocator_api->printf(ta, "%s\\%s.txt", output_dir, output);
			tm_symbols_dump_file_or_dir_to_file(tm_allocator_api->system, input_file, output_file);
		}
		else
			tm_symbols_dump_file_or_dir_to_user(tm_allocator_api->system, input_file);
	}

	const tm_clock_o end_time = tm_os_api->time->now();
	const float elapsed = (float)tm_os_api->time->delta(end_time, start_time);
	printf_loud("\ndbgutils: done, took %.3f s\n", elapsed);
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	return EXIT_SUCCESS;
}