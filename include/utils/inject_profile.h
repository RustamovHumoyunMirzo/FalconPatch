#ifndef FPATCH_INJECT_PROFILE_H
#define FPATCH_INJECT_PROFILE_H

#include <stddef.h>

#define FPATCH_PATH_MAX 1024
#define FPATCH_MAX_NATIVE_INPUTS 128
#define FPATCH_MAX_LUA_INPUTS 128
#define FPATCH_MAX_ASSETS 128
#define FPATCH_MAX_SPLITS 64
#define FPATCH_MAX_DEX_PATCHES 128

typedef enum {
    FPATCH_DEX_PATCH_RETURN_TRUE = 1,
    FPATCH_DEX_PATCH_RETURN_FALSE,
    FPATCH_DEX_PATCH_RETURN_ZERO,
    FPATCH_DEX_PATCH_RETURN_NULL,
    FPATCH_DEX_PATCH_RETURN_VOID,
    FPATCH_DEX_PATCH_REPLACE_STRING
} FpatchDexPatchAction;

typedef struct {
    char target[512];
    char method[512];
    FpatchDexPatchAction action;
    char string_from[1024];
    char string_to[1024];
} FpatchDexPatch;

typedef struct {
    char path[FPATCH_PATH_MAX];
    char abi[32];
    char name[128];
    char init[128];
    char lua_module[128];
} FpatchNativeInput;

typedef struct {
    char path[FPATCH_PATH_MAX];
    char module[128];
    int entry;
} FpatchLuaInput;

typedef struct {
    char source[FPATCH_PATH_MAX];
    char output[FPATCH_PATH_MAX];
    char artifacts[FPATCH_PATH_MAX];
    char profile_name[128];
    char strategy[32];
    char bootstrap_language[16];
    int random_libname;
    int no_sign;

    char keystore[FPATCH_PATH_MAX];
    char key_alias[128];
    char store_password[256];
    char key_password[256];

    FpatchNativeInput native[FPATCH_MAX_NATIVE_INPUTS];
    size_t native_count;
    FpatchLuaInput lua[FPATCH_MAX_LUA_INPUTS];
    size_t lua_count;
    char assets[FPATCH_MAX_ASSETS][FPATCH_PATH_MAX];
    size_t asset_count;
    char splits[FPATCH_MAX_SPLITS][FPATCH_PATH_MAX];
    size_t split_count;
    FpatchDexPatch dex_patches[FPATCH_MAX_DEX_PATCHES];
    size_t dex_patch_count;
} FpatchInjectProfile;

void fpatch_profile_init(FpatchInjectProfile *profile);
int fpatch_profile_load(const char *path, FpatchInjectProfile *profile,
                        char *error, size_t error_size);
int fpatch_profile_validate(const FpatchInjectProfile *profile,
                            char *error, size_t error_size);
int fpatch_profile_add_native(FpatchInjectProfile *profile, const char *path,
                              char *error, size_t error_size);
int fpatch_profile_add_lua(FpatchInjectProfile *profile, const char *path,
                           int entry, char *error, size_t error_size);
int fpatch_profile_add_asset(FpatchInjectProfile *profile, const char *path,
                             char *error, size_t error_size);
int fpatch_profile_add_split(FpatchInjectProfile *profile, const char *path,
                             char *error, size_t error_size);

#endif
