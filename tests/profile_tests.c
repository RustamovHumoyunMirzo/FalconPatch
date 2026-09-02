#include "utils/inject_profile.h"
#include "utils/payload_archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FPATCH_SOURCE_DIR
#define FPATCH_SOURCE_DIR "."
#endif

static int make_path(char *output, size_t output_size, const char *name) {
    int written = snprintf(output, output_size, "%s/%s", FPATCH_SOURCE_DIR, name);
    return written >= 0 && (size_t)written < output_size;
}

static int test_json_profile(void) {
    FpatchInjectProfile *profile = (FpatchInjectProfile *)malloc(sizeof(*profile));
    char path[FPATCH_PATH_MAX];
    char error[512] = "";
    int passed = 0;

    if (!profile || !make_path(path, sizeof(path), "examples/example_fp_profile.json")) {
        goto done;
    }
    fpatch_profile_init(profile);
    if (!fpatch_profile_load(path, profile, error, sizeof(error))) {
        fprintf(stderr, "JSON profile: %s\n", error);
        goto done;
    }
    passed = profile->native_count == 2 && profile->lua_count == 2 &&
             profile->asset_count == 1 && profile->split_count == 1 &&
             profile->random_libname == 1 && strcmp(profile->strategy, "auto") == 0;

done:
    free(profile);
    return passed;
}

static int test_yaml_payload(void) {
    FpatchInjectProfile *profile = (FpatchInjectProfile *)malloc(sizeof(*profile));
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    char path[FPATCH_PATH_MAX];
    char error[512] = "";
    int passed = 0;

    if (!profile || !make_path(path, sizeof(path), "examples/example_fp_profile.yaml")) {
        goto done;
    }
    fpatch_profile_init(profile);
    if (!fpatch_profile_load(path, profile, error, sizeof(error)) ||
        !fpatch_build_payload(profile, &payload, &payload_size, error, sizeof(error))) {
        fprintf(stderr, "YAML profile/payload: %s\n", error);
        goto done;
    }
    passed = profile->lua_count == 2 && profile->lua[1].entry == 1 &&
             profile->no_sign == 1 && payload_size > 12 &&
             memcmp(payload, "FPB1", 4) == 0 && payload[8] == 2;

done:
    free(payload);
    free(profile);
    return passed;
}

static int test_native_abi_metadata(void) {
    FpatchInjectProfile *profile = (FpatchInjectProfile *)malloc(sizeof(*profile));
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    char error[512] = "";
    const char expected[] = "arm64-v8a|start_module";
    unsigned int name_size;
    unsigned int aux_size;
    int passed = 0;

    if (!profile) {
        return 0;
    }
    fpatch_profile_init(profile);
    profile->native_count = 1;
    snprintf(profile->native[0].path, sizeof(profile->native[0].path), "libsample.so");
    snprintf(profile->native[0].abi, sizeof(profile->native[0].abi), "arm64-v8a");
    snprintf(profile->native[0].init, sizeof(profile->native[0].init), "start_module");
    if (!fpatch_build_payload(profile, &payload, &payload_size, error, sizeof(error))) {
        fprintf(stderr, "Native payload: %s\n", error);
        goto done;
    }
    name_size = (unsigned int)payload[14] | ((unsigned int)payload[15] << 8);
    aux_size = (unsigned int)payload[16] | ((unsigned int)payload[17] << 8) |
               ((unsigned int)payload[18] << 16) | ((unsigned int)payload[19] << 24);
    passed = payload_size > 28 + name_size + aux_size && payload[12] == 2 &&
             aux_size == strlen(expected) &&
             memcmp(payload + 28 + name_size, expected, aux_size) == 0;

done:
    free(payload);
    free(profile);
    return passed;
}

static int test_artifact_profile_path(void) {
    FpatchInjectProfile *profile = (FpatchInjectProfile *)malloc(sizeof(*profile));
    char path[FPATCH_PATH_MAX];
    char expected[FPATCH_PATH_MAX];
    char error[512] = "";
    int passed = 0;

    if (!profile ||
        !make_path(path, sizeof(path), "tests/artifact_profile.yaml") ||
        !make_path(expected, sizeof(expected), "tests/bundles/linux-arm64.tar.gz")) {
        goto done;
    }
    fpatch_profile_init(profile);
    if (!fpatch_profile_load(path, profile, error, sizeof(error))) {
        fprintf(stderr, "Artifact profile: %s\n", error);
        goto done;
    }
    passed = strcmp(profile->artifacts, expected) == 0;

done:
    free(profile);
    return passed;
}

int main(void) {
    if (!test_json_profile()) {
        fprintf(stderr, "JSON profile test failed.\n");
        return 1;
    }
    if (!test_yaml_payload()) {
        fprintf(stderr, "YAML payload test failed.\n");
        return 1;
    }
    if (!test_native_abi_metadata()) {
        fprintf(stderr, "Native ABI metadata test failed.\n");
        return 1;
    }
    if (!test_artifact_profile_path()) {
        fprintf(stderr, "Artifact profile path test failed.\n");
        return 1;
    }
    puts("Profile and payload tests passed.");
    return 0;
}
