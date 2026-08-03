#ifndef FPATCH_APK_SIGNER_H
#define FPATCH_APK_SIGNER_H

#include "utils/inject_profile.h"

#include <stddef.h>

int fpatch_prepare_keystore(const FpatchInjectProfile *profile,
                            const char *output_directory,
                            char *keystore, size_t keystore_size,
                            char *error, size_t error_size);
int fpatch_align_and_sign_apk(const char *input, const char *output,
                              const FpatchInjectProfile *profile,
                              const char *keystore,
                              char *error, size_t error_size);

#endif
