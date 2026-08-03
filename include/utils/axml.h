#ifndef FPATCH_AXML_H
#define FPATCH_AXML_H

#include <stddef.h>

int fpatch_axml_add_bootstrap(const unsigned char *data, size_t size,
                              const char *package_name,
                              const char *runtime_library,
                              const char *bootstrap_language,
                              int make_debuggable,
                              unsigned char **output, size_t *output_size,
                              char *error, size_t error_size);

#endif
