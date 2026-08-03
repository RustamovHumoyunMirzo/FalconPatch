#include "fp_internal.h"

#include <lauxlib.h>
#include <stdio.h>
#include <string.h>

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
    jclass context_class;
    jmethodID method;
    jstring value;

    if (!env || !g_fp_runtime.context) {
        lua_pushnil(state);
        return 1;
    }
    context_class = (*env)->GetObjectClass(env, g_fp_runtime.context);
    method = context_class
        ? (*env)->GetMethodID(env, context_class, "getPackageName", "()Ljava/lang/String;")
        : NULL;
    value = method
        ? (jstring)(*env)->CallObjectMethod(env, g_fp_runtime.context, method)
        : NULL;
    clear_exception(env);
    if (context_class) {
        (*env)->DeleteLocalRef(env, context_class);
    }
    push_java_string(state, env, value);
    if (value) {
        (*env)->DeleteLocalRef(env, value);
    }
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

static int lua_call_static_string(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *method_name = luaL_checkstring(state, 2);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;
    jmethodID method;
    jstring result;
    size_t i;

    if (!env) {
        lua_pushnil(state);
        lua_pushliteral(state, "JNI is unavailable");
        return 2;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    snprintf(jni_name, sizeof(jni_name), "%s", class_name);
    for (i = 0; jni_name[i]; i++) {
        if (jni_name[i] == '.') {
            jni_name[i] = '/';
        }
    }
    target = (*env)->FindClass(env, jni_name);
    method = target
        ? (*env)->GetStaticMethodID(env, target, method_name, "()Ljava/lang/String;")
        : NULL;
    result = method
        ? (jstring)(*env)->CallStaticObjectMethod(env, target, method)
        : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        if (target) {
            (*env)->DeleteLocalRef(env, target);
        }
        lua_pushnil(state);
        lua_pushliteral(state, "JNI call failed");
        return 2;
    }
    push_java_string(state, env, result);
    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }
    (*env)->DeleteLocalRef(env, target);
    return 1;
}

int fp_open_jni_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"package_name", lua_package_name},
        {"sdk_int", lua_sdk_int},
        {"call_static_string", lua_call_static_string},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
