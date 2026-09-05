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

static int lua_poll(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring event;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "pollEvent", "()Ljava/lang/String;")
        : NULL;
    event = method ? (jstring)(*env)->CallStaticObjectMethod(env, bridge, method) : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        event = NULL;
    }
    push_java_string(state, env, event);
    if (event) {
        (*env)->DeleteLocalRef(env, event);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return 1;
}

static int lua_drain(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    int index = 1;

    lua_newtable(state);
    if (!env) {
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "pollEvent", "()Ljava/lang/String;")
        : NULL;
    while (method) {
        jstring event = (jstring)(*env)->CallStaticObjectMethod(env, bridge, method);
        if ((*env)->ExceptionCheck(env) || !event) {
            clear_exception(env);
            break;
        }
        push_java_string(state, env, event);
        lua_seti(state, -2, index++);
        (*env)->DeleteLocalRef(env, event);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return 1;
}

static int lua_emit(lua_State *state) {
    const char *event = luaL_checkstring(state, 1);
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_event;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "emitEvent", "(Ljava/lang/String;)V")
        : NULL;
    java_event = method ? (*env)->NewStringUTF(env, event) : NULL;
    if (method && java_event) {
        (*env)->CallStaticVoidMethod(env, bridge, method, java_event);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        lua_pushboolean(state, 0);
    } else {
        lua_pushboolean(state, method && java_event);
    }
    if (java_event) {
        (*env)->DeleteLocalRef(env, java_event);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    return 1;
}

static int element_id(lua_State *state, int index) {
    int id;
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "id");
    id = (int)luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    return id;
}

static int register_element_event(lua_State *state, const char *kind) {
    int id = element_id(state, 1);
    const char *event_name = luaL_optstring(state, 2, "");
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_kind;
    jstring java_event;
    jboolean ok = JNI_FALSE;

    if (!env || id <= 0) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = fp_runtime_bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "setElementEvent",
                                   "(ILjava/lang/String;Ljava/lang/String;)Z")
        : NULL;
    java_kind = method ? (*env)->NewStringUTF(env, kind) : NULL;
    java_event = method ? (*env)->NewStringUTF(env, event_name) : NULL;
    if (method && java_kind && java_event) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             id, java_kind, java_event);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_kind) {
        (*env)->DeleteLocalRef(env, java_kind);
    }
    if (java_event) {
        (*env)->DeleteLocalRef(env, java_event);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int lua_on_click(lua_State *state) {
    return register_element_event(state, "click");
}

static int lua_on_down(lua_State *state) {
    return register_element_event(state, "down");
}

static int lua_on_up(lua_State *state) {
    return register_element_event(state, "up");
}

static int lua_on_drag(lua_State *state) {
    return register_element_event(state, "drag");
}

static int lua_on_hover(lua_State *state) {
    return register_element_event(state, "hover");
}

int fp_open_events_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"poll", lua_poll},
        {"next", lua_poll},
        {"drain", lua_drain},
        {"all", lua_drain},
        {"emit", lua_emit},
        {"onClick", lua_on_click},
        {"onDown", lua_on_down},
        {"onTouch", lua_on_down},
        {"onUp", lua_on_up},
        {"onRelease", lua_on_up},
        {"onDrag", lua_on_drag},
        {"onMove", lua_on_drag},
        {"onHover", lua_on_hover},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
