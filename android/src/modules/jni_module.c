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

static void normalize_class_name(const char *class_name, char *output, size_t output_size) {
    size_t i;

    snprintf(output, output_size, "%s", class_name);
    for (i = 0; output[i]; i++) {
        if (output[i] == '.') {
            output[i] = '/';
        }
    }
}

static int lua_class_exists(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    normalize_class_name(class_name, jni_name, sizeof(jni_name));
    target = (*env)->FindClass(env, jni_name);
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        target = NULL;
    }
    if (target) {
        (*env)->DeleteLocalRef(env, target);
    }
    lua_pushboolean(state, target != NULL);
    return 1;
}

static int lua_get_static_string(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *field_name = luaL_checkstring(state, 2);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;
    jfieldID field;
    jstring result;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    normalize_class_name(class_name, jni_name, sizeof(jni_name));
    target = (*env)->FindClass(env, jni_name);
    field = target
        ? (*env)->GetStaticFieldID(env, target, field_name, "Ljava/lang/String;")
        : NULL;
    result = field ? (jstring)(*env)->GetStaticObjectField(env, target, field) : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = NULL;
    }
    push_java_string(state, env, result);
    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }
    if (target) {
        (*env)->DeleteLocalRef(env, target);
    }
    return 1;
}

static int lua_get_static_int(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *field_name = luaL_checkstring(state, 2);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;
    jfieldID field;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    normalize_class_name(class_name, jni_name, sizeof(jni_name));
    target = (*env)->FindClass(env, jni_name);
    field = target ? (*env)->GetStaticFieldID(env, target, field_name, "I") : NULL;
    if ((*env)->ExceptionCheck(env) || !field) {
        clear_exception(env);
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, (*env)->GetStaticIntField(env, target, field));
    }
    if (target) {
        (*env)->DeleteLocalRef(env, target);
    }
    return 1;
}

static int lua_get_static_boolean(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *field_name = luaL_checkstring(state, 2);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;
    jfieldID field;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    normalize_class_name(class_name, jni_name, sizeof(jni_name));
    target = (*env)->FindClass(env, jni_name);
    field = target ? (*env)->GetStaticFieldID(env, target, field_name, "Z") : NULL;
    if ((*env)->ExceptionCheck(env) || !field) {
        clear_exception(env);
        lua_pushnil(state);
    } else {
        lua_pushboolean(state, (*env)->GetStaticBooleanField(env, target, field) == JNI_TRUE);
    }
    if (target) {
        (*env)->DeleteLocalRef(env, target);
    }
    return 1;
}

static int lua_call_static_int(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *method_name = luaL_checkstring(state, 2);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;
    jmethodID method;

    if (!env) {
        lua_pushnil(state);
        lua_pushliteral(state, "JNI is unavailable");
        return 2;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    normalize_class_name(class_name, jni_name, sizeof(jni_name));
    target = (*env)->FindClass(env, jni_name);
    method = target ? (*env)->GetStaticMethodID(env, target, method_name, "()I") : NULL;
    if ((*env)->ExceptionCheck(env) || !method) {
        clear_exception(env);
        if (target) {
            (*env)->DeleteLocalRef(env, target);
        }
        lua_pushnil(state);
        lua_pushliteral(state, "JNI call failed");
        return 2;
    }
    lua_pushinteger(state, (*env)->CallStaticIntMethod(env, target, method));
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        lua_pop(state, 1);
        lua_pushnil(state);
        lua_pushliteral(state, "JNI call failed");
        (*env)->DeleteLocalRef(env, target);
        return 2;
    }
    (*env)->DeleteLocalRef(env, target);
    return 1;
}

static int lua_call_static_boolean(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *method_name = luaL_checkstring(state, 2);
    char jni_name[512];
    JNIEnv *env = fp_get_env();
    jclass target;
    jmethodID method;
    jboolean result;

    if (!env) {
        lua_pushnil(state);
        lua_pushliteral(state, "JNI is unavailable");
        return 2;
    }
    if (strlen(class_name) >= sizeof(jni_name)) {
        return luaL_error(state, "class name is too long");
    }
    normalize_class_name(class_name, jni_name, sizeof(jni_name));
    target = (*env)->FindClass(env, jni_name);
    method = target ? (*env)->GetStaticMethodID(env, target, method_name, "()Z") : NULL;
    if ((*env)->ExceptionCheck(env) || !method) {
        clear_exception(env);
        if (target) {
            (*env)->DeleteLocalRef(env, target);
        }
        lua_pushnil(state);
        lua_pushliteral(state, "JNI call failed");
        return 2;
    }
    result = (*env)->CallStaticBooleanMethod(env, target, method);
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        lua_pushnil(state);
        lua_pushliteral(state, "JNI call failed");
        (*env)->DeleteLocalRef(env, target);
        return 2;
    }
    (*env)->DeleteLocalRef(env, target);
    lua_pushboolean(state, result == JNI_TRUE);
    return 1;
}

int fp_open_jni_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"package_name", lua_package_name},
        {"sdk_int", lua_sdk_int},
        {"class_exists", lua_class_exists},
        {"get_static_string", lua_get_static_string},
        {"get_static_int", lua_get_static_int},
        {"get_static_boolean", lua_get_static_boolean},
        {"call_static_string", lua_call_static_string},
        {"call_static_int", lua_call_static_int},
        {"call_static_boolean", lua_call_static_boolean},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
