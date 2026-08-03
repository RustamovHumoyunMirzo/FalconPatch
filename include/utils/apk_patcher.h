#ifndef FPATCH_APK_PATCHER_H
#define FPATCH_APK_PATCHER_H

#include "utils/inject_profile.h"

#include <stddef.h>

#define FPATCH_MAX_OUTPUT_APKS (FPATCH_MAX_SPLITS + 1)

typedef struct {
    char output_paths[FPATCH_MAX_OUTPUT_APKS][FPATCH_PATH_MAX];
    size_t output_count;
    char runtime_library[64];
    char artifact_package[128];
    char strategy_used[32];
    char bootstrap_language[16];
    char target_abis[16][32];
    size_t target_abi_count;
    int resigned;
} FpatchInjectResult;

int fpatch_inject_apk(const FpatchInjectProfile *requested_profile,
                      FpatchInjectResult *result,
                      char *error, size_t error_size);

#endif
