#ifndef FPATCH_EMBEDDED_RESOURCES_H
#define FPATCH_EMBEDDED_RESOURCES_H

#include <stddef.h>

typedef struct {
    const unsigned char *data;
    size_t size;
} FpatchEmbeddedResource;

FpatchEmbeddedResource fpatch_embedded_find(const char *kind, const char *name);

#endif
