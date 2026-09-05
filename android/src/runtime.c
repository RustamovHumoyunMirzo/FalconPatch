#include "fp_internal.h"

#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <lauxlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPATCH_LOG_TAG "FalconPatch"
#define FPATCH_PAYLOAD_ASSET "falconpatch/runtime.bin"

FpRuntime g_fp_runtime = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};

void fp_log(int priority, const char *tag, const char *message) {
    __android_log_write(priority, tag ? tag : FPATCH_LOG_TAG,
                        message ? message : "");
}

void fp_logf(int priority, const char *tag, const char *format, ...) {
    char message[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    fp_log(priority, tag, message);
}

JNIEnv *fp_get_env(void) {
    JNIEnv *env = NULL;

    if (!g_fp_runtime.vm) {
        return NULL;
    }
    if ((*g_fp_runtime.vm)->GetEnv(g_fp_runtime.vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if ((*g_fp_runtime.vm)->AttachCurrentThread(g_fp_runtime.vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    return env;
}

jclass fp_runtime_bridge_class(JNIEnv *env) {
    jclass local;

    if (!env) {
        return NULL;
    }
    if (g_fp_runtime.bridge_class) {
        return (jclass)(*env)->NewLocalRef(env, g_fp_runtime.bridge_class);
    }
    local = (*env)->FindClass(env, "dev/falconpatch/runtime/RuntimeBridge");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (!local) {
        return NULL;
    }
    g_fp_runtime.bridge_class = (jclass)(*env)->NewGlobalRef(env, local);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, local);
        return NULL;
    }
    return local;
}

static int read_payload_asset(AAssetManager *manager, unsigned char **data, size_t *size) {
    AAsset *asset;
    off_t length;
    int read_count;

    asset = AAssetManager_open(manager, FPATCH_PAYLOAD_ASSET, AASSET_MODE_BUFFER);
    if (!asset) {
        fp_log(ANDROID_LOG_ERROR, FPATCH_LOG_TAG, "Payload asset is missing.");
        return 0;
    }
    length = AAsset_getLength(asset);
    if (length <= 0 || (uint64_t)length > SIZE_MAX) {
        AAsset_close(asset);
        return 0;
    }
    *data = (unsigned char *)malloc((size_t)length);
    if (!*data) {
        AAsset_close(asset);
        return 0;
    }
    read_count = AAsset_read(asset, *data, (size_t)length);
    AAsset_close(asset);
    if (read_count != length) {
        free(*data);
        *data = NULL;
        return 0;
    }
    *size = (size_t)length;
    return 1;
}

int fp_runtime_start(JNIEnv *env, jobject context) {
    jobject asset_manager_object;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    int result = 0;

    pthread_mutex_lock(&g_fp_runtime.lock);
    if (g_fp_runtime.started) {
        pthread_mutex_unlock(&g_fp_runtime.lock);
        return 1;
    }

    g_fp_runtime.context = (*env)->NewGlobalRef(env, context);
    if (!g_fp_runtime.context) {
        goto done;
    }
    {
        jclass context_class = (*env)->GetObjectClass(env, context);
        jmethodID get_assets = context_class
            ? (*env)->GetMethodID(env, context_class, "getAssets", "()Landroid/content/res/AssetManager;")
            : NULL;
        asset_manager_object = get_assets
            ? (*env)->CallObjectMethod(env, context, get_assets)
            : NULL;
        if (context_class) {
            (*env)->DeleteLocalRef(env, context_class);
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        asset_manager_object = NULL;
    }
    if (!asset_manager_object) {
        goto done;
    }
    g_fp_runtime.asset_manager = AAssetManager_fromJava(env, asset_manager_object);
    (*env)->DeleteLocalRef(env, asset_manager_object);
    if (!g_fp_runtime.asset_manager ||
        !read_payload_asset(g_fp_runtime.asset_manager, &payload, &payload_size)) {
        free(payload);
        goto done;
    }
    if (!fp_archive_parse(payload, payload_size, &g_fp_runtime.archive)) {
        free(payload);
        goto done;
    }

    g_fp_runtime.lua = luaL_newstate();
    if (!g_fp_runtime.lua || !fp_lua_open_restricted(g_fp_runtime.lua)) {
        goto done;
    }
    fp_lua_register_module("jni", fp_open_jni_module);
    fp_lua_register_module("gui", fp_open_gui_module);
    fp_lua_register_module("ui", fp_open_ui_module);
    fp_lua_register_module("assets", fp_open_assets_module);
    fp_lua_register_module("events", fp_open_events_module);
    fp_lua_register_module("intent", fp_open_intent_module);
    fp_lua_register_module("app", fp_open_app_module);
    if (!fp_load_native_modules()) {
        fp_log(ANDROID_LOG_ERROR, FPATCH_LOG_TAG, "One or more native modules failed to load.");
        goto done;
    }
    fp_lua_register_all_modules(g_fp_runtime.lua);
    if (!fp_lua_run_archive(g_fp_runtime.lua, &g_fp_runtime.archive)) {
        fp_log(ANDROID_LOG_ERROR, FPATCH_LOG_TAG, "Lua payload initialization failed.");
        goto done;
    }
    g_fp_runtime.started = 1;
    result = 1;
    fp_log(ANDROID_LOG_INFO, FPATCH_LOG_TAG, "Runtime initialized.");

done:
    if (!result) {
        fp_log(ANDROID_LOG_ERROR, FPATCH_LOG_TAG,
               "Runtime disabled after an initialization error; the host app will continue.");
    }
    pthread_mutex_unlock(&g_fp_runtime.lock);
    return result;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_fp_runtime.vm = vm;
    return JNI_VERSION_1_6;
}
