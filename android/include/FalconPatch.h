#ifndef FALCONPATCH_EXTENSION_H
#define FALCONPATCH_EXTENSION_H

#include <jni.h>
#include <stdint.h>
#include <lua.h>

#if defined(__GNUC__)
#define FPATCH_EXPORT __attribute__((visibility("default")))
#else
#define FPATCH_EXPORT
#endif

#define FPATCH_EXTENSION_ABI 1u

typedef struct FalconPatchHostApi {
    uint32_t abi_version;
    JavaVM *java_vm;
    jobject application_context;
    JNIEnv *(*get_jni_env)(void);
    void (*log)(int priority, const char *tag, const char *message);
    int (*register_lua_module)(const char *name, lua_CFunction open_function);
} FalconPatchHostApi;

typedef struct FalconPatchLuaModule {
    uint32_t abi_version;
    const char *name;
    lua_CFunction open_function;
} FalconPatchLuaModule;

/* Optional automatic native initializer exported by a user library. */
FPATCH_EXPORT int falconpatch_init(const FalconPatchHostApi *host);

/* Optional Lua extension descriptor exported by a user library. */
FPATCH_EXPORT const FalconPatchLuaModule *falconpatch_lua_module(void);

#endif
