#include "fp_internal.h"

#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
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

static jstring make_string(JNIEnv *env, const char *value) {
    return (*env)->NewStringUTF(env, value ? value : "");
}

static jobjectArray make_string_array(lua_State *state, JNIEnv *env, int first_index) {
    int top = lua_gettop(state);
    int count = top >= first_index ? top - first_index + 1 : 0;
    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jobjectArray array;
    int i;

    if ((*env)->ExceptionCheck(env) || !string_class) {
        clear_exception(env);
        return NULL;
    }
    array = (*env)->NewObjectArray(env, count, string_class, NULL);
    for (i = 0; array && i < count; i++) {
        jstring value;
        if (lua_isnil(state, first_index + i)) {
            value = make_string(env, "");
        } else if (lua_isboolean(state, first_index + i)) {
            value = make_string(env, lua_toboolean(state, first_index + i) ? "true" : "false");
        } else if (lua_istable(state, first_index + i)) {
            char handle[32];
            int id;
            lua_getfield(state, first_index + i, "id");
            id = lua_isinteger(state, -1) ? (int)lua_tointeger(state, -1) : 0;
            lua_pop(state, 1);
            if (id > 0) {
                snprintf(handle, sizeof(handle), "@%d", id);
                value = make_string(env, handle);
            } else {
                value = make_string(env, "");
            }
        } else {
            size_t length;
            const char *text;
            luaL_tolstring(state, first_index + i, &length);
            text = lua_tostring(state, -1);
            (void)length;
            value = make_string(env, text);
            lua_pop(state, 1);
        }
        if (!value) {
            clear_exception(env);
            break;
        }
        (*env)->SetObjectArrayElement(env, array, i, value);
        (*env)->DeleteLocalRef(env, value);
    }
    (*env)->DeleteLocalRef(env, string_class);
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        return NULL;
    }
    return array;
}

static int push_reflect_result(lua_State *state, JNIEnv *env, jstring encoded);

static int call_reflect(lua_State *state, const char *method_name,
                        const char *signature, int first_arg_index,
                        const char *class_name, int object_id,
                        const char *member, const char *member_signature,
                        const char *value) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_class;
    jstring java_member;
    jstring java_signature;
    jstring java_value;
    jobjectArray args;
    jstring result;
    int returns;

    if (!env) {
        lua_pushnil(state);
        lua_pushliteral(state, "JNI is unavailable");
        return 2;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, method_name, signature) : NULL;
    java_class = method ? make_string(env, class_name) : NULL;
    java_member = method ? make_string(env, member) : NULL;
    java_signature = method ? make_string(env, member_signature) : NULL;
    java_value = method ? make_string(env, value) : NULL;
    args = method ? make_string_array(state, env, first_arg_index) : NULL;
    result = method && args
        ? (jstring)(*env)->CallStaticObjectMethod(env, bridge, method, java_class,
                                                  object_id, java_member,
                                                  java_signature, args, java_value)
        : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = NULL;
    }
    returns = push_reflect_result(state, env, result);
    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }
    if (args) {
        (*env)->DeleteLocalRef(env, args);
    }
    if (java_value) {
        (*env)->DeleteLocalRef(env, java_value);
    }
    if (java_signature) {
        (*env)->DeleteLocalRef(env, java_signature);
    }
    if (java_member) {
        (*env)->DeleteLocalRef(env, java_member);
    }
    if (java_class) {
        (*env)->DeleteLocalRef(env, java_class);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return returns;
}

static void set_method(lua_State *state, const char *name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

static void class_name_from_table(lua_State *state, int index,
                                  char *output, size_t output_size) {
    const char *class_name;

    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "className");
    class_name = luaL_checkstring(state, -1);
    snprintf(output, output_size, "%s", class_name);
    lua_pop(state, 1);
}

static int object_id_from_table(lua_State *state, int index) {
    int id;
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "id");
    id = (int)luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    return id;
}

void fp_lua_push_java_object(lua_State *state, int id, const char *class_name);

static int push_reflect_result(lua_State *state, JNIEnv *env, jstring encoded) {
    const char *text;
    int result_count = 1;

    if (!encoded) {
        lua_pushnil(state);
        lua_pushliteral(state, "reflection call failed");
        return 2;
    }
    text = (*env)->GetStringUTFChars(env, encoded, NULL);
    if (!text) {
        clear_exception(env);
        lua_pushnil(state);
        lua_pushliteral(state, "reflection result decode failed");
        return 2;
    }
    if (strncmp(text, "E:", 2) == 0) {
        lua_pushnil(state);
        lua_pushstring(state, text + 2);
        result_count = 2;
    } else if (strcmp(text, "N") == 0) {
        lua_pushnil(state);
    } else if (strncmp(text, "Z:", 2) == 0) {
        lua_pushboolean(state, strcmp(text + 2, "true") == 0 || strcmp(text + 2, "1") == 0);
    } else if (strncmp(text, "I:", 2) == 0) {
        lua_pushinteger(state, (lua_Integer)strtoll(text + 2, NULL, 10));
    } else if (strncmp(text, "D:", 2) == 0) {
        lua_pushnumber(state, (lua_Number)strtod(text + 2, NULL));
    } else if (strncmp(text, "S:", 2) == 0) {
        lua_pushstring(state, text + 2);
    } else if (strncmp(text, "O:", 2) == 0) {
        const char *separator = strchr(text + 2, ':');
        int id = separator ? atoi(text + 2) : 0;
        fp_lua_push_java_object(state, id, separator ? separator + 1 : "java.lang.Object");
    } else {
        lua_pushstring(state, text);
    }
    (*env)->ReleaseStringUTFChars(env, encoded, text);
    return result_count;
}

static int lua_java_class_exists(lua_State *state) {
    char class_name[512];

    class_name_from_table(state, 1, class_name, sizeof(class_name));
    return call_reflect(
        state, "reflectClassExists",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        2, class_name, 0, "", "", "");
}

static int lua_java_call_static(lua_State *state) {
    char class_name[512];
    const char *method_name = luaL_checkstring(state, 2);
    const char *method_signature = luaL_checkstring(state, 3);

    class_name_from_table(state, 1, class_name, sizeof(class_name));
    return call_reflect(
        state, "reflectStatic",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        4, class_name, 0, method_name, method_signature, "");
}

static int lua_java_new(lua_State *state) {
    char class_name[512];
    const char *constructor_signature = luaL_checkstring(state, 2);

    class_name_from_table(state, 1, class_name, sizeof(class_name));
    return call_reflect(
        state, "reflectNew",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        3, class_name, 0, "", constructor_signature, "");
}

static int lua_java_get_static(lua_State *state) {
    char class_name[512];
    const char *field_name = luaL_checkstring(state, 2);
    const char *field_signature = luaL_optstring(state, 3, "");

    class_name_from_table(state, 1, class_name, sizeof(class_name));
    return call_reflect(
        state, "reflectGetStatic",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        4, class_name, 0, field_name, field_signature, "");
}

static int lua_java_set_static(lua_State *state) {
    char class_name[512];
    const char *field_name = luaL_checkstring(state, 2);
    const char *field_signature = luaL_checkstring(state, 3);
    const char *value;

    class_name_from_table(state, 1, class_name, sizeof(class_name));
    luaL_tolstring(state, 4, NULL);
    value = lua_tostring(state, -1);
    return call_reflect(
        state, "reflectSetStatic",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        5, class_name, 0, field_name, field_signature, value);
}

static int lua_object_call(lua_State *state) {
    int id = object_id_from_table(state, 1);
    const char *method_name = luaL_checkstring(state, 2);
    const char *method_signature = luaL_checkstring(state, 3);
    return call_reflect(
        state, "reflectObject",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        4, "", id, method_name, method_signature, "");
}

static int lua_object_get(lua_State *state) {
    int id = object_id_from_table(state, 1);
    const char *field_name = luaL_checkstring(state, 2);
    const char *field_signature = luaL_optstring(state, 3, "");
    return call_reflect(
        state, "reflectGetObject",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        4, "", id, field_name, field_signature, "");
}

static int lua_object_set(lua_State *state) {
    int id = object_id_from_table(state, 1);
    const char *field_name = luaL_checkstring(state, 2);
    const char *field_signature = luaL_checkstring(state, 3);
    const char *value;
    luaL_tolstring(state, 4, NULL);
    value = lua_tostring(state, -1);
    return call_reflect(
        state, "reflectSetObject",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        5, "", id, field_name, field_signature, value);
}

static int lua_object_release(lua_State *state) {
    int id = object_id_from_table(state, 1);
    return call_reflect(
        state, "reflectRelease",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        2, "", id, "", "", "");
}

static int lua_object_tostring(lua_State *state) {
    int id = object_id_from_table(state, 1);
    lua_pushfstring(state, "JavaObject(%d)", id);
    return 1;
}

void fp_lua_push_java_object(lua_State *state, int id, const char *class_name) {
    lua_newtable(state);
    lua_pushinteger(state, id);
    lua_setfield(state, -2, "id");
    lua_pushstring(state, class_name ? class_name : "java.lang.Object");
    lua_setfield(state, -2, "className");
    set_method(state, "call", lua_object_call);
    set_method(state, "get", lua_object_get);
    set_method(state, "set", lua_object_set);
    set_method(state, "release", lua_object_release);
    set_method(state, "toString", lua_object_tostring);
}

static int lua_java_use(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);

    lua_newtable(state);
    lua_pushstring(state, class_name);
    lua_setfield(state, -2, "className");
    set_method(state, "exists", lua_java_class_exists);
    set_method(state, "callStatic", lua_java_call_static);
    set_method(state, "getStatic", lua_java_get_static);
    set_method(state, "setStatic", lua_java_set_static);
    set_method(state, "new", lua_java_new);
    return 1;
}

void fp_lua_install_java_global(lua_State *state) {
    lua_newtable(state);
    set_method(state, "use", lua_java_use);
    lua_setglobal(state, "Java");
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
        {"use", lua_java_use},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
