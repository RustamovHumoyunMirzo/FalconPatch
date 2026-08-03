#ifndef FPATCH_PAYLOAD_ARCHIVE_H
#define FPATCH_PAYLOAD_ARCHIVE_H

#include "utils/inject_profile.h"

#include <stddef.h>

int fpatch_native_filename(const FpatchNativeInput *input,
                           char *output, size_t output_size);
int fpatch_build_payload(const FpatchInjectProfile *profile,
                         unsigned char **data, size_t *size,
                         char *error, size_t error_size);

#endif
