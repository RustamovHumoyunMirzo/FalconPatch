#ifndef FPATCH_APK_DETACHER_H
#define FPATCH_APK_DETACHER_H

#include "utils/inject_profile.h"

#include <stddef.h>

#define FPATCH_MAX_DETACH_ABIS 16

typedef struct {
    char target[FPATCH_PATH_MAX];
    char output[FPATCH_PATH_MAX];
    char library[128];
    char abis[FPATCH_MAX_DETACH_ABIS][32];
    size_t abi_count;
    int sign;
    int smart_repair;
    char keystore[FPATCH_PATH_MAX];
    char key_alias[128];
    char store_password[256];
    char key_password[256];
} FpatchDetachRequest;

typedef struct {
    char output[FPATCH_PATH_MAX];
    char library[128];
    char removed_entries[FPATCH_MAX_DETACH_ABIS][256];
    size_t removed_count;
    size_t repaired_load_calls;
    size_t repaired_native_calls;
    size_t skipped_native_calls;
    size_t jni_exports;
    int detected_registered_jni;
    int stripped_falconpatch_payload;
    int resigned;
} FpatchDetachResult;

void fpatch_detach_request_init(FpatchDetachRequest *request);
size_t fpatch_repair_dex_load_calls(unsigned char *data, size_t size,
                                    const char *module_name,
                                    const char *library_name);
size_t fpatch_repair_dex_jni_calls(unsigned char *data, size_t size,
                                   const char * const *jni_exports,
                                   size_t jni_export_count,
                                   size_t *skipped_calls);
size_t fpatch_repair_dex_registered_jni_calls(unsigned char *data, size_t size,
                                              const char * const *native_strings,
                                              size_t native_string_count,
                                              size_t *skipped_calls);
int fpatch_detach_apk(const FpatchDetachRequest *request,
                      FpatchDetachResult *result,
                      char *error, size_t error_size);

#endif
