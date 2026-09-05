#ifndef FPATCH_DEX_TRANSFORMER_H
#define FPATCH_DEX_TRANSFORMER_H

#include "utils/inject_profile.h"

#include <stddef.h>

typedef struct {
    size_t methods_patched;
    size_t strings_replaced;
} FpatchDexTransformStats;

int fpatch_transform_dex(unsigned char *data, size_t size,
                         const FpatchDexPatch *patches, size_t patch_count,
                         size_t *applied_counts,
                         FpatchDexTransformStats *stats,
                         char *error, size_t error_size);

#endif
