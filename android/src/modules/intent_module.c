#include "fp_internal.h"

#include <lauxlib.h>

static void clear_exception(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static int call_intent(lua_State *state, const char *method_name) {
    const char *action = luaL_checkstring(state, 1);
    const char *uri = luaL_optstring(state, 2, "");
    const char *package_name = luaL_optstring(state, 3, "");
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_action;
    jstring java_uri;
    jstring java_package;
    jboolean ok = JNI_FALSE;

    if (!env || !g_fp_runtime.context) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, method_name,
                                   "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z")
        : NULL;
    java_action = method ? (*env)->NewStringUTF(env, action) : NULL;
    java_uri = method ? (*env)->NewStringUTF(env, uri) : NULL;
    java_package = method ? (*env)->NewStringUTF(env, package_name) : NULL;
    if (method && java_action && java_uri && java_package) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method, g_fp_runtime.context,
                                             java_action, java_uri, java_package);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_action) {
        (*env)->DeleteLocalRef(env, java_action);
    }
    if (java_uri) {
        (*env)->DeleteLocalRef(env, java_uri);
    }
    if (java_package) {
        (*env)->DeleteLocalRef(env, java_package);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int lua_start_activity(lua_State *state) {
    return call_intent(state, "startActivityIntent");
}

static int lua_broadcast(lua_State *state) {
    return call_intent(state, "sendBroadcastIntent");
}

int fp_open_intent_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"start_activity", lua_start_activity},
        {"broadcast", lua_broadcast},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
