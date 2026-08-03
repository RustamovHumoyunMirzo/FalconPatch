#ifndef FPATCH_PROCESS_H
#define FPATCH_PROCESS_H

#include <stddef.h>

int fpatch_find_executable(const char *name, char *output, size_t output_size);
int fpatch_find_android_build_tool(const char *name, char *output, size_t output_size);
int fpatch_find_apksigner_jar(char *output, size_t output_size);
int fpatch_run_process(const char *executable, const char *const argv[],
                       char *error, size_t error_size);

#endif
