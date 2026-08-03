#include "fp_internal.h"

#include <lauxlib.h>

static int lua_toast(lua_State *state) {
    const char *message = luaL_checkstring(state, 1);
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID show_toast;
    jstring java_message;

    if (!env || !g_fp_runtime.context) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = (*env)->FindClass(env, "dev/falconpatch/runtime/RuntimeBridge");
    show_toast = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "showToast",
                                   "(Landroid/content/Context;Ljava/lang/String;)V")
        : NULL;
    java_message = show_toast ? (*env)->NewStringUTF(env, message) : NULL;
    if (show_toast && java_message) {
        (*env)->CallStaticVoidMethod(env, bridge, show_toast,
                                     g_fp_runtime.context, java_message);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        lua_pushboolean(state, 0);
    } else {
        lua_pushboolean(state, show_toast && java_message);
    }
    if (java_message) {
        (*env)->DeleteLocalRef(env, java_message);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return 1;
}

int fp_open_gui_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"toast", lua_toast},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
