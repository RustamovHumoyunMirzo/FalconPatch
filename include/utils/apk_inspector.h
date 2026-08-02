#ifndef FPATCH_APK_INSPECTOR_H
#define FPATCH_APK_INSPECTOR_H

#include <stddef.h>
#include <stdint.h>

#define MAX_NATIVE_LIBS 512
#define MAX_NATIVE_METHODS 512
#define MAX_LOAD_CALLS 512

typedef struct {
    char dex_file[64];
    char class_name[256];
    char method_name[128];
    char params[512];
    char return_type[128];
} NativeMethod;

typedef struct {
    char dex_file[64];
    char class_name[256];
    char method_name[128];
    char api[16];
    char argument[256];
} NativeLoadCall;

typedef struct {
    char path[512];
    char abi[32];
    unsigned long long size;
    unsigned long long compressed_size;
    uint16_t compression_method;
    uint16_t elf_machine;
    int elf_class;
    int elf_valid;
} NativeLib;

typedef struct {
    char package_name[256];
    char version_name[128];
    uint32_t version_code;
    uint32_t min_sdk;
    uint32_t target_sdk;
    char application_class[256];
    int debuggable;
    int test_only;
    int has_manifest;
} ManifestInfo;

typedef struct {
    const char *path;
    int is_apk;
    int dex_count;
    int native_lib_count;
    int has_manifest;
    int has_resources;
    int has_falcon_bootstrap;
    int sig_v2;
    int sig_v3;
    int sig_v31;
    NativeLib native_libs[MAX_NATIVE_LIBS];
    NativeMethod native_methods[MAX_NATIVE_METHODS];
    NativeLoadCall load_calls[MAX_LOAD_CALLS];
    int native_method_count;
    int load_call_count;
    char abis[16][32];
    size_t abi_count;
    unsigned char cert_sha256[32];
    int has_cert;
    ManifestInfo manifest;
} ApkInfo;

int fpatch_inspect_apk(const char *apk_path, ApkInfo *info);
void fpatch_print_csv_abis(const ApkInfo *info);
void fpatch_print_inspect_report(const ApkInfo *info);
void fpatch_print_ndk_report(const ApkInfo *info);

#endif
