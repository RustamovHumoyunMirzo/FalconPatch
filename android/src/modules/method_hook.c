#include "fp_internal.h"

#include <android/log.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPATCH_MAX_METHOD_HOOKS 128

typedef struct {
    unsigned int id;
    int callback_ref;
    int active;
    int enabled;
    unsigned int dispatch_depth;
    unsigned long calls;
    unsigned long failures;
    char class_name[512];
    char method_name[256];
    char signature[512];
} FpMethodHook;

static FpMethodHook hooks[FPATCH_MAX_METHOD_HOOKS];
static pthread_mutex_t hooks_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int next_hook_id = 1;

static void normalize_class_name(const char *input, char *output, size_t output_size) {
    size_t i;

    snprintf(output, output_size, "%s", input ? input : "");
    for (i = 0; output[i]; i++) {
        if (output[i] == '/') {
            output[i] = '.';
        }
    }
}

static FpMethodHook *find_hook(unsigned int id) {
    size_t i;

    for (i = 0; i < FPATCH_MAX_METHOD_HOOKS; i++) {
        if (hooks[i].active && hooks[i].id == id) {
            return &hooks[i];
        }
    }
    return NULL;
}

static unsigned int hook_id_from_table(lua_State *state) {
    unsigned int id;

    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "id");
    id = (unsigned int)luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    return id;
}

static int lua_hook_remove(lua_State *state) {
    unsigned int id = hook_id_from_table(state);
    int callback_ref = LUA_NOREF;
    FpMethodHook *hook;

    pthread_mutex_lock(&hooks_lock);
    hook = find_hook(id);
    if (hook) {
        callback_ref = hook->callback_ref;
        memset(hook, 0, sizeof(*hook));
    }
    pthread_mutex_unlock(&hooks_lock);
    if (callback_ref != LUA_NOREF) {
        luaL_unref(state, LUA_REGISTRYINDEX, callback_ref);
    }
    lua_pushboolean(state, callback_ref != LUA_NOREF);
    return 1;
}

static int set_hook_enabled(lua_State *state, int enabled) {
    unsigned int id = hook_id_from_table(state);
    FpMethodHook *hook;
    int changed = 0;

    pthread_mutex_lock(&hooks_lock);
    hook = find_hook(id);
    if (hook) {
        hook->enabled = enabled;
        changed = 1;
    }
    pthread_mutex_unlock(&hooks_lock);
    lua_pushboolean(state, changed);
    return 1;
}

static int lua_hook_enable(lua_State *state) {
    return set_hook_enabled(state, 1);
}

static int lua_hook_disable(lua_State *state) {
    return set_hook_enabled(state, 0);
}

static int lua_hook_is_enabled(lua_State *state) {
    unsigned int id = hook_id_from_table(state);
    FpMethodHook *hook;
    int enabled = 0;

    pthread_mutex_lock(&hooks_lock);
    hook = find_hook(id);
    enabled = hook && hook->enabled;
    pthread_mutex_unlock(&hooks_lock);
    lua_pushboolean(state, enabled);
    return 1;
}

static int lua_hook_stats(lua_State *state) {
    unsigned int id = hook_id_from_table(state);
    FpMethodHook snapshot;
    FpMethodHook *hook;

    memset(&snapshot, 0, sizeof(snapshot));
    pthread_mutex_lock(&hooks_lock);
    hook = find_hook(id);
    if (hook) {
        snapshot = *hook;
    }
    pthread_mutex_unlock(&hooks_lock);
    if (!snapshot.active) {
        lua_pushnil(state);
        return 1;
    }
    lua_newtable(state);
    lua_pushinteger(state, (lua_Integer)snapshot.calls);
    lua_setfield(state, -2, "calls");
    lua_pushinteger(state, (lua_Integer)snapshot.failures);
    lua_setfield(state, -2, "failures");
    lua_pushboolean(state, snapshot.enabled);
    lua_setfield(state, -2, "enabled");
    return 1;
}

static void set_method(lua_State *state, const char *name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

static void push_hook_handle(lua_State *state, unsigned int id) {
    lua_newtable(state);
    lua_pushinteger(state, id);
    lua_setfield(state, -2, "id");
    set_method(state, "remove", lua_hook_remove);
    set_method(state, "enable", lua_hook_enable);
    set_method(state, "disable", lua_hook_disable);
    set_method(state, "isEnabled", lua_hook_is_enabled);
    set_method(state, "stats", lua_hook_stats);
}

static int lua_hook_method(lua_State *state) {
    const char *class_name = luaL_checkstring(state, 1);
    const char *method_name = luaL_checkstring(state, 2);
    const char *signature = "";
    int callback_index = 3;
    int callback_ref;
    unsigned int id;
    size_t i;
    FpMethodHook *slot = NULL;

    if (lua_type(state, 3) == LUA_TSTRING) {
        signature = lua_tostring(state, 3);
        callback_index = 4;
    }
    luaL_checktype(state, callback_index, LUA_TFUNCTION);
    if (strlen(class_name) >= sizeof(hooks[0].class_name) ||
        strlen(method_name) >= sizeof(hooks[0].method_name) ||
        strlen(signature) >= sizeof(hooks[0].signature)) {
        return luaL_error(state, "hook class, method, or signature is too long");
    }

    lua_pushvalue(state, callback_index);
    callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);
    pthread_mutex_lock(&hooks_lock);
    for (i = 0; i < FPATCH_MAX_METHOD_HOOKS; i++) {
        if (!hooks[i].active) {
            slot = &hooks[i];
            break;
        }
    }
    if (!slot) {
        pthread_mutex_unlock(&hooks_lock);
        luaL_unref(state, LUA_REGISTRYINDEX, callback_ref);
        return luaL_error(state, "method hook limit reached");
    }
    id = next_hook_id++;
    if (next_hook_id == 0) {
        next_hook_id = 1;
    }
    memset(slot, 0, sizeof(*slot));
    slot->id = id;
    slot->callback_ref = callback_ref;
    slot->active = 1;
    slot->enabled = 1;
    normalize_class_name(class_name, slot->class_name, sizeof(slot->class_name));
    snprintf(slot->method_name, sizeof(slot->method_name), "%s", method_name);
    snprintf(slot->signature, sizeof(slot->signature), "%s", signature);
    pthread_mutex_unlock(&hooks_lock);

    push_hook_handle(state, id);
    return 1;
}

static int lua_hook_capabilities(lua_State *state) {
    lua_newtable(state);
    lua_pushliteral(state, "reflection-bridge");
    lua_setfield(state, -2, "backend");
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, "synchronous");
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, "originalResult");
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "directArtMethod");
    lua_pushinteger(state, FPATCH_MAX_METHOD_HOOKS);
    lua_setfield(state, -2, "maxHooks");
    return 1;
}

void fp_lua_install_method_hook_globals(lua_State *state) {
    lua_getglobal(state, "fpatch");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    set_method(state, "hookMethod", lua_hook_method);
    set_method(state, "hookCapabilities", lua_hook_capabilities);
    lua_pop(state, 1);
}

static void push_encoded_result(lua_State *state, const char *encoded) {
    if (!encoded || strcmp(encoded, "N") == 0) {
        lua_pushnil(state);
    } else if (strncmp(encoded, "Z:", 2) == 0) {
        lua_pushboolean(state, strcmp(encoded + 2, "true") == 0 ||
                               strcmp(encoded + 2, "1") == 0);
    } else if (strncmp(encoded, "I:", 2) == 0) {
        lua_pushinteger(state, (lua_Integer)strtoll(encoded + 2, NULL, 10));
    } else if (strncmp(encoded, "D:", 2) == 0) {
        lua_pushnumber(state, (lua_Number)strtod(encoded + 2, NULL));
    } else if (strncmp(encoded, "S:", 2) == 0) {
        lua_pushstring(state, encoded + 2);
    } else if (strncmp(encoded, "O:", 2) == 0) {
        const char *separator = strchr(encoded + 2, ':');
        int id = separator ? atoi(encoded + 2) : 0;
        fp_lua_push_java_object(state, id,
                                separator ? separator + 1 : "java.lang.Object");
    } else {
        lua_pushstring(state, encoded);
    }
}

static char *encode_lua_result(lua_State *state, int index, const char *original) {
    const char *prefix = original ? original : "N";
    char buffer[128];
    const char *value;
    char *result;

    if (lua_isnil(state, index)) {
        return NULL;
    }
    if (lua_isboolean(state, index)) {
        value = lua_toboolean(state, index) ? "Z:true" : "Z:false";
    } else if (lua_isinteger(state, index)) {
        snprintf(buffer, sizeof(buffer), "I:%lld",
                 (long long)lua_tointeger(state, index));
        value = buffer;
    } else if (lua_isnumber(state, index)) {
        snprintf(buffer, sizeof(buffer), "D:%.17g", (double)lua_tonumber(state, index));
        value = buffer;
    } else if (lua_istable(state, index)) {
        int id;
        const char *class_name;
        lua_getfield(state, index, "id");
        id = lua_isinteger(state, -1) ? (int)lua_tointeger(state, -1) : 0;
        lua_pop(state, 1);
        lua_getfield(state, index, "className");
        class_name = lua_isstring(state, -1) ? lua_tostring(state, -1) : "java.lang.Object";
        snprintf(buffer, sizeof(buffer), "O:%d:%.96s", id, class_name);
        lua_pop(state, 1);
        value = id > 0 ? buffer : prefix;
    } else {
        size_t length;
        const char *text = luaL_tolstring(state, index, &length);
        result = (char *)malloc(length + 3);
        if (!result) {
            lua_pop(state, 1);
            return NULL;
        }
        memcpy(result, "S:", 2);
        memcpy(result + 2, text, length + 1);
        lua_pop(state, 1);
        return result;
    }
    result = (char *)malloc(strlen(value) + 1);
    if (result) {
        strcpy(result, value);
    }
    return result;
}

static char *copy_java_string(JNIEnv *env, jstring value) {
    const char *utf;
    char *copy;

    if (!value) {
        return NULL;
    }
    utf = (*env)->GetStringUTFChars(env, value, NULL);
    if (!utf) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    copy = (char *)malloc(strlen(utf) + 1);
    if (copy) {
        strcpy(copy, utf);
    }
    (*env)->ReleaseStringUTFChars(env, value, utf);
    return copy;
}

jstring fp_method_hook_dispatch(JNIEnv *env, jstring java_class_name, jint object_id,
                                jstring java_method_name, jstring java_signature,
                                jstring encoded_result) {
    char *class_name = copy_java_string(env, java_class_name);
    char *method_name = copy_java_string(env, java_method_name);
    char *signature = copy_java_string(env, java_signature);
    char *current = copy_java_string(env, encoded_result);
    lua_State *state = g_fp_runtime.lua;
    unsigned int dispatch_ids[FPATCH_MAX_METHOD_HOOKS];
    size_t dispatch_count = 0;
    size_t i;

    if (!current) {
        current = (char *)malloc(2);
        if (current) {
            strcpy(current, "N");
        }
    }
    if (!state || !class_name || !method_name || !signature || !current) {
        goto done;
    }
    for (i = 0; class_name[i]; i++) {
        if (class_name[i] == '/') {
            class_name[i] = '.';
        }
    }
    pthread_mutex_lock(&hooks_lock);
    for (i = 0; i < FPATCH_MAX_METHOD_HOOKS; i++) {
        FpMethodHook *hook = &hooks[i];
        if (hook->active && hook->enabled && hook->dispatch_depth == 0 &&
            strcmp(hook->class_name, class_name) == 0 &&
            strcmp(hook->method_name, method_name) == 0 &&
            (!hook->signature[0] || strcmp(hook->signature, signature) == 0)) {
            dispatch_ids[dispatch_count++] = hook->id;
        }
    }
    pthread_mutex_unlock(&hooks_lock);

    for (i = 0; i < dispatch_count; i++) {
        FpMethodHook *hook;
        int callback_ref = LUA_NOREF;
        int top;
        char *replacement;

        pthread_mutex_lock(&hooks_lock);
        hook = find_hook(dispatch_ids[i]);
        if (hook && hook->enabled && hook->dispatch_depth == 0) {
            hook->dispatch_depth++;
            hook->calls++;
            callback_ref = hook->callback_ref;
        }
        pthread_mutex_unlock(&hooks_lock);
        if (callback_ref == LUA_NOREF) {
            continue;
        }

        top = lua_gettop(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, callback_ref);
        if (object_id > 0) {
            fp_lua_push_java_object(state, object_id, class_name);
        } else {
            lua_pushnil(state);
        }
        push_encoded_result(state, current);
        if (lua_pcall(state, 2, 1, 0) != LUA_OK) {
            fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Hook", "%s.%s: %s",
                    class_name, method_name, lua_tostring(state, -1));
            pthread_mutex_lock(&hooks_lock);
            hook = find_hook(dispatch_ids[i]);
            if (hook) {
                hook->failures++;
                hook->dispatch_depth--;
            }
            pthread_mutex_unlock(&hooks_lock);
            lua_settop(state, top);
            continue;
        }
        replacement = encode_lua_result(state, -1, current);
        if (replacement) {
            free(current);
            current = replacement;
        }
        lua_settop(state, top);
        pthread_mutex_lock(&hooks_lock);
        hook = find_hook(dispatch_ids[i]);
        if (hook) {
            hook->dispatch_depth--;
        }
        pthread_mutex_unlock(&hooks_lock);
    }

done:
    {
        jstring result = (*env)->NewStringUTF(env, current ? current : "N");
        free(current);
        free(signature);
        free(method_name);
        free(class_name);
        return result;
    }
}
