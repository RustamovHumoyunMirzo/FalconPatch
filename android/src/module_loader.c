#include "fp_internal.h"

#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*FpNativeInit)(const FalconPatchHostApi *host);
typedef const FalconPatchLuaModule *(*FpLuaDescriptor)(void);
typedef jint (*FpJniOnLoad)(JavaVM *vm, void *reserved);

#ifndef FPATCH_ANDROID_ABI
#define FPATCH_ANDROID_ABI "unknown"
#endif

static const FalconPatchHostApi g_host_api = {
    FPATCH_EXTENSION_ABI,
    NULL,
    NULL,
    fp_get_env,
    fp_log,
    fp_lua_register_module
};

static int register_descriptor(void *handle, const char *configured_name) {
    FpLuaDescriptor descriptor_function;
    const FalconPatchLuaModule *descriptor;
    const char *name;

    descriptor_function = (FpLuaDescriptor)dlsym(handle, "falconpatch_lua_module");
    if (!descriptor_function) {
        return 1;
    }
    descriptor = descriptor_function();
    if (!descriptor || descriptor->abi_version != FPATCH_EXTENSION_ABI ||
        !descriptor->open_function) {
        return 0;
    }
    name = configured_name && configured_name[0] ? configured_name : descriptor->name;
    return name && fp_lua_register_module(name, descriptor->open_function);
}

static int load_record(const FpArchiveRecord *record) {
    FalconPatchHostApi host = g_host_api;
    char library_name[256];
    void *handle;
    FpNativeInit initializer = NULL;
    const char *initializer_name = record->aux;
    const char *separator = record->aux ? strchr(record->aux, '|') : NULL;

    if (separator) {
        size_t abi_size = (size_t)(separator - record->aux);
        if (strlen(FPATCH_ANDROID_ABI) != abi_size ||
            strncmp(record->aux, FPATCH_ANDROID_ABI, abi_size) != 0) {
            return 1;
        }
        initializer_name = separator + 1;
    }

    if (record->size == 0 || record->size >= sizeof(library_name)) {
        return 0;
    }
    memcpy(library_name, record->data, record->size);
    library_name[record->size] = '\0';
    handle = dlopen(library_name, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Loader", "Cannot load %s: %s",
                library_name, dlerror());
        return 0;
    }
    if (g_fp_runtime.module_handle_count >= FPATCH_MAX_LOADED_MODULES) {
        dlclose(handle);
        return 0;
    }
    g_fp_runtime.module_handles[g_fp_runtime.module_handle_count++] = handle;
    host.java_vm = g_fp_runtime.vm;
    host.application_context = g_fp_runtime.context;

    if (initializer_name && initializer_name[0]) {
        initializer = (FpNativeInit)dlsym(handle, initializer_name);
        if (!initializer) {
            fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Loader",
                    "Initializer %s was not found in %s.", initializer_name, library_name);
            return 0;
        }
    } else {
        initializer = (FpNativeInit)dlsym(handle, "falconpatch_init");
    }
    if (initializer && initializer(&host) != 0) {
        fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Loader",
                "Initializer failed for %s.", library_name);
        return 0;
    }
    if (!initializer) {
        FpJniOnLoad jni_on_load = (FpJniOnLoad)dlsym(handle, "JNI_OnLoad");
        if (jni_on_load && jni_on_load(g_fp_runtime.vm, NULL) < JNI_VERSION_1_6) {
            return 0;
        }
    }
    if (!register_descriptor(handle, record->name)) {
        fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Loader",
                "Invalid Lua extension descriptor in %s.", library_name);
        return 0;
    }
    return 1;
}

int fp_load_native_modules(void) {
    size_t i;

    for (i = 0; i < g_fp_runtime.archive.record_count; i++) {
        const FpArchiveRecord *record = &g_fp_runtime.archive.records[i];
        if (record->type == FPATCH_RECORD_NATIVE && !load_record(record)) {
            return 0;
        }
    }
    return 1;
}
