#include "fp_internal.h"

#include <lauxlib.h>

static void clear_exception(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static int push_java_string(lua_State *state, JNIEnv *env, jstring value) {
    const char *text;

    if (!value) {
        lua_pushnil(state);
        return 1;
    }
    text = (*env)->GetStringUTFChars(env, value, NULL);
    if (!text) {
        clear_exception(env);
        lua_pushnil(state);
        return 1;
    }
    lua_pushstring(state, text);
    (*env)->ReleaseStringUTFChars(env, value, text);
    return 1;
}

static int lua_package_name(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring result;

    if (!env || !g_fp_runtime.context) {
        lua_pushnil(state);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "packageName",
                                   "(Landroid/content/Context;)Ljava/lang/String;")
        : NULL;
    result = method
        ? (jstring)(*env)->CallStaticObjectMethod(env, bridge, method, g_fp_runtime.context)
        : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = NULL;
    }
    push_java_string(state, env, result);
    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return 1;
}

static int lua_current_activity(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring result;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "currentActivityName",
                                   "()Ljava/lang/String;")
        : NULL;
    result = method ? (jstring)(*env)->CallStaticObjectMethod(env, bridge, method) : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = NULL;
    }
    push_java_string(state, env, result);
    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return 1;
}

static int lua_foreground(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jboolean result = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, "isForeground", "()Z") : NULL;
    if (method) {
        result = (*env)->CallStaticBooleanMethod(env, bridge, method);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = JNI_FALSE;
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, result == JNI_TRUE);
    return 1;
}

static int lua_sdk_int(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass version_class;
    jfieldID field;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    version_class = (*env)->FindClass(env, "android/os/Build$VERSION");
    field = version_class
        ? (*env)->GetStaticFieldID(env, version_class, "SDK_INT", "I")
        : NULL;
    if (!field) {
        clear_exception(env);
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, (*env)->GetStaticIntField(env, version_class, field));
    }
    if (version_class) {
        (*env)->DeleteLocalRef(env, version_class);
    }
    return 1;
}

static int lua_info(lua_State *state) {
    lua_newtable(state);
    lua_package_name(state);
    lua_setfield(state, -2, "package_name");
    lua_sdk_int(state);
    lua_setfield(state, -2, "sdk_int");
    lua_current_activity(state);
    lua_setfield(state, -2, "current_activity");
    lua_foreground(state);
    lua_setfield(state, -2, "foreground");
    return 1;
}

int fp_open_app_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"package_name", lua_package_name},
        {"packageName", lua_package_name},
        {"sdk_int", lua_sdk_int},
        {"sdkInt", lua_sdk_int},
        {"current_activity", lua_current_activity},
        {"currentActivity", lua_current_activity},
        {"foreground", lua_foreground},
        {"isForeground", lua_foreground},
        {"info", lua_info},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
