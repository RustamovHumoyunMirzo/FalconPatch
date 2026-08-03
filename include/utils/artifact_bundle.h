#ifndef FPATCH_ARTIFACT_BUNDLE_H
#define FPATCH_ARTIFACT_BUNDLE_H

#include <stddef.h>

#define FPATCH_ARTIFACT_SCHEMA_VERSION 1u
#define FPATCH_RUNTIME_API_VERSION 1u
#define FPATCH_MAX_ARTIFACT_FILES 128
#define FPATCH_ARTIFACT_PATH_MAX 256

typedef struct {
    char kind[32];
    char name[64];
    char path[FPATCH_ARTIFACT_PATH_MAX];
    char sha256[65];
    const unsigned char *data;
    size_t size;
} FpatchArtifactFile;

typedef struct {
    unsigned int schema_version;
    unsigned int runtime_api;
    char falconpatch_version[32];
    char package[128];
    char platform[32];
    char arch[32];
    FpatchArtifactFile files[FPATCH_MAX_ARTIFACT_FILES];
    size_t file_count;
    unsigned char *archive_data;
    size_t archive_size;
} FpatchArtifactBundle;

void fpatch_artifact_bundle_init(FpatchArtifactBundle *bundle);
void fpatch_artifact_bundle_free(FpatchArtifactBundle *bundle);
int fpatch_artifact_bundle_load(const char *path, FpatchArtifactBundle *bundle,
                                char *error, size_t error_size);
const FpatchArtifactFile *fpatch_artifact_bundle_find(
    const FpatchArtifactBundle *bundle, const char *kind, const char *name);

#endif
