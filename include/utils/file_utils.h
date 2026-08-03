#ifndef FPATCH_FILE_UTILS_H
#define FPATCH_FILE_UTILS_H

#include <stddef.h>

int fpatch_file_exists(const char *path);
int fpatch_directory_exists(const char *path);
int fpatch_make_directories(const char *path);
int fpatch_read_file(const char *path, unsigned char **data, size_t *size,
                     char *error, size_t error_size);
int fpatch_write_file(const char *path, const void *data, size_t size,
                      char *error, size_t error_size);
int fpatch_copy_file(const char *source, const char *target,
                     char *error, size_t error_size);
int fpatch_replace_file(const char *source, const char *target,
                        char *error, size_t error_size);
const char *fpatch_path_basename(const char *path);
void fpatch_path_dirname(const char *path, char *output, size_t output_size);
int fpatch_path_join(char *output, size_t output_size,
                     const char *left, const char *right);
int fpatch_default_output_path(const char *source, char *output, size_t output_size);
int fpatch_random_alpha(char *output, size_t length);
int fpatch_elf_abi(const char *path, char *abi, size_t abi_size,
                   char *error, size_t error_size);

#endif
