#ifndef FPATCH_APK_ARCHIVE_H
#define FPATCH_APK_ARCHIVE_H

#include "utils/inject_profile.h"

#include <stddef.h>

#define FPATCH_MAX_TARGET_ABIS 16

typedef struct {
    char abi[32];
    const unsigned char *data;
    size_t size;
} FpatchRuntimePayload;

typedef struct {
    const FpatchInjectProfile *profile;
    const char *package_name;
    const char *runtime_library;
    const unsigned char *bootstrap_dex;
    size_t bootstrap_dex_size;
    const unsigned char *payload;
    size_t payload_size;
    char target_abis[FPATCH_MAX_TARGET_ABIS][32];
    size_t target_abi_count;
    FpatchRuntimePayload runtimes[FPATCH_MAX_TARGET_ABIS];
    size_t runtime_count;
    size_t *dex_patch_applied;
    size_t *dex_methods_patched;
    size_t *dex_strings_replaced;
} FpatchArchivePatch;

int fpatch_patch_base_archive(const char *source_path, const char *output_path,
                              const FpatchArchivePatch *patch,
                              char *strategy_used, size_t strategy_used_size,
                              char *error, size_t error_size);
int fpatch_repack_split_archive(const char *source_path, const char *output_path,
                                char *error, size_t error_size);

#endif
