#ifndef FPATCH_ANDROID_INTERNAL_H
#define FPATCH_ANDROID_INTERNAL_H

#include "FalconPatch.h"

#include <android/asset_manager.h>
#include <jni.h>
#include <lua.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define FPATCH_ARCHIVE_MAGIC "FPB1"
#define FPATCH_ARCHIVE_VERSION 1u
#define FPATCH_RECORD_LUA 1u
#define FPATCH_RECORD_NATIVE 2u
#define FPATCH_RECORD_ASSET 3u
#define FPATCH_RECORD_ENTRY 1u
#define FPATCH_MAX_LOADED_MODULES 128
#define FPATCH_MAX_LUA_MODULES 128

typedef struct {
    uint8_t type;
    uint8_t flags;
    char *name;
    char *aux;
    unsigned char *data;
    size_t size;
} FpArchiveRecord;

typedef struct {
    unsigned char *storage;
    size_t storage_size;
    FpArchiveRecord *records;
    size_t record_count;
} FpArchive;

typedef struct {
    char name[128];
    lua_CFunction open_function;
} FpLuaModule;

typedef struct {
    JavaVM *vm;
    jclass bridge_class;
    jobject context;
    AAssetManager *asset_manager;
    lua_State *lua;
    FpArchive archive;
    void *module_handles[FPATCH_MAX_LOADED_MODULES];
    size_t module_handle_count;
    FpLuaModule lua_modules[FPATCH_MAX_LUA_MODULES];
    size_t lua_module_count;
    pthread_mutex_t lock;
    int started;
} FpRuntime;

extern FpRuntime g_fp_runtime;

uint16_t fp_read_u16(const unsigned char *data);
uint32_t fp_read_u32(const unsigned char *data);
uint64_t fp_read_u64(const unsigned char *data);
int fp_archive_parse(unsigned char *storage, size_t size, FpArchive *archive);
void fp_archive_free(FpArchive *archive);

void fp_log(int priority, const char *tag, const char *message);
void fp_logf(int priority, const char *tag, const char *format, ...);
JNIEnv *fp_get_env(void);
jclass fp_runtime_bridge_class(JNIEnv *env);
int fp_runtime_start(JNIEnv *env, jobject context);

int fp_load_native_modules(void);
int fp_lua_open_restricted(lua_State *state);
int fp_lua_register_module(const char *name, lua_CFunction open_function);
void fp_lua_register_all_modules(lua_State *state);
int fp_lua_run_archive(lua_State *state, const FpArchive *archive);

int fp_open_jni_module(lua_State *state);
int fp_open_gui_module(lua_State *state);
int fp_open_ui_module(lua_State *state);
int fp_open_events_module(lua_State *state);
int fp_open_intent_module(lua_State *state);
int fp_open_app_module(lua_State *state);
int fp_open_background_worker_module(lua_State *state);

#endif
