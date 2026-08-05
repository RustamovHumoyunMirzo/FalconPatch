#include "fp_internal.h"

#include <lauxlib.h>
#include <string.h>

#define FP_UI_FILL_SCREEN_WIDTH (-1)
#define FP_UI_FILL_SCREEN_HEIGHT (-1)
#define FP_UI_WRAP_CONTENT (-2)

static void clear_exception(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static jclass bridge_class(JNIEnv *env) {
    return (*env)->FindClass(env, "dev/falconpatch/runtime/RuntimeBridge");
}

static jstring make_string(JNIEnv *env, const char *value) {
    return (*env)->NewStringUTF(env, value ? value : "");
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

static int table_id(lua_State *state, int index, const char *kind) {
    int id;
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "id");
    id = (int)luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (id <= 0) {
        luaL_error(state, "invalid %s handle", kind);
    }
    return id;
}

static int call_static_boolean_context_string(lua_State *state, const char *name,
                                              const char *signature,
                                              const char *first,
                                              const char *second) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring one;
    jstring two;
    jboolean ok = JNI_FALSE;

    if (!env || !g_fp_runtime.context) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, name, signature) : NULL;
    one = method ? make_string(env, first) : NULL;
    two = method ? make_string(env, second) : NULL;
    if (method && one && two) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             g_fp_runtime.context, one, two);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (one) {
        (*env)->DeleteLocalRef(env, one);
    }
    if (two) {
        (*env)->DeleteLocalRef(env, two);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int call_overlay_int(lua_State *state, int overlay_id,
                            const char *key, int value) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_key;
    jboolean ok = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "setOverlayInt",
                                   "(ILjava/lang/String;I)Z")
        : NULL;
    java_key = method ? make_string(env, key) : NULL;
    if (method && java_key) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             overlay_id, java_key, value);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_key) {
        (*env)->DeleteLocalRef(env, java_key);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int call_element_int(lua_State *state, int element_id,
                            const char *key, int value) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_key;
    jboolean ok = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "setElementInt",
                                   "(ILjava/lang/String;I)Z")
        : NULL;
    java_key = method ? make_string(env, key) : NULL;
    if (method && java_key) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             element_id, java_key, value);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_key) {
        (*env)->DeleteLocalRef(env, java_key);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int call_element_string(lua_State *state, int element_id,
                               const char *key, const char *value) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_key;
    jstring java_value;
    jboolean ok = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "setElementString",
                                   "(ILjava/lang/String;Ljava/lang/String;)Z")
        : NULL;
    java_key = method ? make_string(env, key) : NULL;
    java_value = method ? make_string(env, value) : NULL;
    if (method && java_key && java_value) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             element_id, java_key, java_value);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_key) {
        (*env)->DeleteLocalRef(env, java_key);
    }
    if (java_value) {
        (*env)->DeleteLocalRef(env, java_value);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int call_element_bool(lua_State *state, int element_id,
                             const char *key, int value) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_key;
    jboolean ok = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "setElementBoolean",
                                   "(ILjava/lang/String;Z)Z")
        : NULL;
    java_key = method ? make_string(env, key) : NULL;
    if (method && java_key) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             element_id, java_key,
                                             value ? JNI_TRUE : JNI_FALSE);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_key) {
        (*env)->DeleteLocalRef(env, java_key);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int call_element_float(lua_State *state, int element_id,
                              const char *key, lua_Number value) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_key;
    jboolean ok = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "setElementFloat",
                                   "(ILjava/lang/String;F)Z")
        : NULL;
    java_key = method ? make_string(env, key) : NULL;
    if (method && java_key) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             element_id, java_key, (jfloat)value);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (java_key) {
        (*env)->DeleteLocalRef(env, java_key);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static void set_method(lua_State *state, const char *name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

static void push_element(lua_State *state, int id);
static int lua_overlay_add_text(lua_State *state);
static int lua_overlay_add_button(lua_State *state);
static int lua_overlay_add_checkbox(lua_State *state);
static int lua_overlay_add_switch(lua_State *state);
static int lua_overlay_add_list(lua_State *state);
static int lua_overlay_add_glsurface(lua_State *state);
static int lua_overlay_add_grid(lua_State *state);
static int lua_overlay_add_hlayout(lua_State *state);
static int lua_overlay_add_vlayout(lua_State *state);
static int lua_overlay_add_image(lua_State *state);
static int lua_overlay_add_webview(lua_State *state);

static int lua_element_set(lua_State *state) {
    int id = table_id(state, 1, "element");
    const char *key = luaL_checkstring(state, 2);

    if (lua_isboolean(state, 3)) {
        return call_element_bool(state, id, key, lua_toboolean(state, 3));
    }
    if (lua_isnumber(state, 3)) {
        return call_element_int(state, id, key, (int)lua_tointeger(state, 3));
    }
    return call_element_string(state, id, key, luaL_checkstring(state, 3));
}

static int lua_element_text(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (lua_gettop(state) >= 2) {
        return call_element_string(state, id, "text", luaL_checkstring(state, 2));
    }
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring key;
    jstring result;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "getElementString",
                                   "(ILjava/lang/String;)Ljava/lang/String;")
        : NULL;
    key = method ? make_string(env, "text") : NULL;
    result = method && key
        ? (jstring)(*env)->CallStaticObjectMethod(env, bridge, method, id, key)
        : NULL;
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = NULL;
    }
    if (key) {
        (*env)->DeleteLocalRef(env, key);
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

static int lua_element_bool_get(lua_State *state, const char *key_name) {
    int id = table_id(state, 1, "element");
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring key;
    jboolean result = JNI_FALSE;

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "getElementBoolean",
                                   "(ILjava/lang/String;)Z")
        : NULL;
    key = method ? make_string(env, key_name) : NULL;
    if (method && key) {
        result = (*env)->CallStaticBooleanMethod(env, bridge, method, id, key);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        result = JNI_FALSE;
    }
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, result == JNI_TRUE);
    return 1;
}

static int lua_element_checked(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (lua_gettop(state) >= 2) {
        return call_element_bool(state, id, "checked", lua_toboolean(state, 2));
    }
    return lua_element_bool_get(state, "checked");
}

static int lua_element_enabled(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (lua_gettop(state) >= 2) {
        return call_element_bool(state, id, "enabled", lua_toboolean(state, 2));
    }
    return lua_element_bool_get(state, "enabled");
}

static int lua_element_draggable(lua_State *state) {
    return call_element_bool(state, table_id(state, 1, "element"),
                             "draggable", lua_toboolean(state, 2));
}

static int lua_element_weight(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (lua_gettop(state) >= 2) {
        return call_element_float(state, id, "weight", luaL_checknumber(state, 2));
    }
    {
        JNIEnv *env = fp_get_env();
        jclass bridge;
        jmethodID method;
        jstring key;
        jfloat result = 0;

        if (!env) {
            lua_pushnumber(state, 0);
            return 1;
        }
        bridge = bridge_class(env);
        method = bridge
            ? (*env)->GetStaticMethodID(env, bridge, "getElementFloat",
                                       "(ILjava/lang/String;)F")
            : NULL;
        key = method ? make_string(env, "weight") : NULL;
        if (method && key) {
            result = (*env)->CallStaticFloatMethod(env, bridge, method, id, key);
        }
        clear_exception(env);
        if (key) {
            (*env)->DeleteLocalRef(env, key);
        }
        if (bridge) {
            (*env)->DeleteLocalRef(env, bridge);
        }
        lua_pushnumber(state, result);
        return 1;
    }
}

static int lua_element_visible(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (lua_gettop(state) >= 2) {
        return call_element_int(state, id, "visible", lua_toboolean(state, 2) ? 1 : 0);
    }
    return lua_element_bool_get(state, "visible");
}

static int lua_element_width(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "width", (int)luaL_checkinteger(state, 2));
}

static int lua_element_height(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "height", (int)luaL_checkinteger(state, 2));
}

static int lua_element_position(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (!call_element_int(state, id, "x", (int)luaL_checkinteger(state, 2)) ||
        !lua_toboolean(state, -1)) {
        return 1;
    }
    lua_pop(state, 1);
    return call_element_int(state, id, "y", (int)luaL_checkinteger(state, 3));
}

static int lua_element_corner(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "corner_radius", (int)luaL_checkinteger(state, 2));
}

static int lua_element_background(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "background", (int)luaL_checkinteger(state, 2));
}

static int lua_element_text_color(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "text_color", (int)luaL_checkinteger(state, 2));
}

static int lua_element_text_size(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "text_size", (int)luaL_checkinteger(state, 2));
}

static int lua_element_padding(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "padding", (int)luaL_checkinteger(state, 2));
}

static int lua_element_columns(lua_State *state) {
    return call_element_int(state, table_id(state, 1, "element"),
                            "columns", (int)luaL_checkinteger(state, 2));
}

static int lua_element_stroke(lua_State *state) {
    int id = table_id(state, 1, "element");
    if (!call_element_int(state, id, "stroke_width", (int)luaL_checkinteger(state, 2)) ||
        !lua_toboolean(state, -1)) {
        return 1;
    }
    lua_pop(state, 1);
    return call_element_int(state, id, "stroke_color", (int)luaL_checkinteger(state, 3));
}

static int lua_element_url(lua_State *state) {
    return call_element_string(state, table_id(state, 1, "element"),
                               "url", luaL_checkstring(state, 2));
}

static int lua_element_html(lua_State *state) {
    return call_element_string(state, table_id(state, 1, "element"),
                               "html", luaL_checkstring(state, 2));
}

static int lua_element_items(lua_State *state) {
    return call_element_string(state, table_id(state, 1, "element"),
                               "items", luaL_checkstring(state, 2));
}

static int lua_element_image(lua_State *state) {
    return call_element_string(state, table_id(state, 1, "element"),
                               "image", luaL_checkstring(state, 2));
}

static void push_element(lua_State *state, int id) {
    lua_newtable(state);
    lua_pushinteger(state, id);
    lua_setfield(state, -2, "id");
    set_method(state, "set", lua_element_set);
    set_method(state, "setText", lua_element_text);
    set_method(state, "getText", lua_element_text);
    set_method(state, "text", lua_element_text);
    set_method(state, "setChecked", lua_element_checked);
    set_method(state, "isChecked", lua_element_checked);
    set_method(state, "checked", lua_element_checked);
    set_method(state, "enabled", lua_element_enabled);
    set_method(state, "visible", lua_element_visible);
    set_method(state, "draggable", lua_element_draggable);
    set_method(state, "width", lua_element_width);
    set_method(state, "height", lua_element_height);
    set_method(state, "position", lua_element_position);
    set_method(state, "setCornerRadius", lua_element_corner);
    set_method(state, "background", lua_element_background);
    set_method(state, "textColor", lua_element_text_color);
    set_method(state, "textSize", lua_element_text_size);
    set_method(state, "padding", lua_element_padding);
    set_method(state, "columns", lua_element_columns);
    set_method(state, "stroke", lua_element_stroke);
    set_method(state, "weight", lua_element_weight);
    set_method(state, "url", lua_element_url);
    set_method(state, "html", lua_element_html);
    set_method(state, "items", lua_element_items);
    set_method(state, "image", lua_element_image);
    set_method(state, "addText", lua_overlay_add_text);
    set_method(state, "addButton", lua_overlay_add_button);
    set_method(state, "addCheckbox", lua_overlay_add_checkbox);
    set_method(state, "addSwitch", lua_overlay_add_switch);
    set_method(state, "addList", lua_overlay_add_list);
    set_method(state, "addOpenGLSurface", lua_overlay_add_glsurface);
    set_method(state, "addGrid", lua_overlay_add_grid);
    set_method(state, "addHLayout", lua_overlay_add_hlayout);
    set_method(state, "addVLayout", lua_overlay_add_vlayout);
    set_method(state, "addImage", lua_overlay_add_image);
    set_method(state, "addWebView", lua_overlay_add_webview);
}

static int lua_overlay_add(lua_State *state, const char *type) {
    int overlay_id = table_id(state, 1, "overlay");
    const char *text = luaL_optstring(state, 2, "");
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_type;
    jstring java_text;
    jint id = 0;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "addElement",
                                   "(ILjava/lang/String;Ljava/lang/String;)I")
        : NULL;
    java_type = method ? make_string(env, type) : NULL;
    java_text = method ? make_string(env, text) : NULL;
    if (method && java_type && java_text) {
        id = (*env)->CallStaticIntMethod(env, bridge, method, overlay_id,
                                         java_type, java_text);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        id = 0;
    }
    if (java_type) {
        (*env)->DeleteLocalRef(env, java_type);
    }
    if (java_text) {
        (*env)->DeleteLocalRef(env, java_text);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    if (id <= 0) {
        lua_pushnil(state);
    } else {
        push_element(state, (int)id);
    }
    return 1;
}

static int lua_overlay_add_text(lua_State *state) {
    return lua_overlay_add(state, "text");
}

static int lua_overlay_add_button(lua_State *state) {
    return lua_overlay_add(state, "button");
}

static int lua_overlay_add_checkbox(lua_State *state) {
    return lua_overlay_add(state, "checkbox");
}

static int lua_overlay_add_switch(lua_State *state) {
    return lua_overlay_add(state, "switch");
}

static int lua_overlay_add_list(lua_State *state) {
    return lua_overlay_add(state, "list");
}

static int lua_overlay_add_glsurface(lua_State *state) {
    return lua_overlay_add(state, "glsurface");
}

static int lua_overlay_add_grid(lua_State *state) {
    return lua_overlay_add(state, "grid");
}

static int lua_overlay_add_hlayout(lua_State *state) {
    return lua_overlay_add(state, "hlayout");
}

static int lua_overlay_add_vlayout(lua_State *state) {
    return lua_overlay_add(state, "vlayout");
}

static int lua_overlay_add_image(lua_State *state) {
    return lua_overlay_add(state, "image");
}

static int lua_overlay_add_webview(lua_State *state) {
    return lua_overlay_add(state, "webview");
}

static int lua_overlay_set(lua_State *state) {
    int id = table_id(state, 1, "overlay");
    const char *key = luaL_checkstring(state, 2);
    return call_overlay_int(state, id, key, (int)luaL_checkinteger(state, 3));
}

static int lua_overlay_width(lua_State *state) {
    return call_overlay_int(state, table_id(state, 1, "overlay"),
                            "width", (int)luaL_checkinteger(state, 2));
}

static int lua_overlay_height(lua_State *state) {
    return call_overlay_int(state, table_id(state, 1, "overlay"),
                            "height", (int)luaL_checkinteger(state, 2));
}

static int lua_overlay_align(lua_State *state) {
    return call_overlay_int(state, table_id(state, 1, "overlay"),
                            "gravity", (int)luaL_checkinteger(state, 2));
}

static int lua_overlay_position(lua_State *state) {
    int id = table_id(state, 1, "overlay");
    if (!call_overlay_int(state, id, "x", (int)luaL_checkinteger(state, 2)) ||
        !lua_toboolean(state, -1)) {
        return 1;
    }
    lua_pop(state, 1);
    return call_overlay_int(state, id, "y", (int)luaL_checkinteger(state, 3));
}

static int lua_overlay_background(lua_State *state) {
    return call_overlay_int(state, table_id(state, 1, "overlay"),
                            "background", (int)luaL_checkinteger(state, 2));
}

static int lua_overlay_alpha(lua_State *state) {
    return call_overlay_int(state, table_id(state, 1, "overlay"),
                            "alpha", (int)luaL_checkinteger(state, 2));
}

static int lua_overlay_visible(lua_State *state) {
    return call_overlay_int(state, table_id(state, 1, "overlay"),
                            "visible", lua_toboolean(state, 2) ? 1 : 0);
}

static int lua_overlay_clear(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jboolean ok = JNI_FALSE;
    int id = table_id(state, 1, "overlay");

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, "clearOverlayById", "(I)Z") : NULL;
    if (method) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method, id);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int lua_overlay_remove(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jboolean ok = JNI_FALSE;
    int id = table_id(state, 1, "overlay");

    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, "removeOverlayById", "(I)Z") : NULL;
    if (method) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method, id);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static void push_overlay(lua_State *state, int id) {
    lua_newtable(state);
    lua_pushinteger(state, id);
    lua_setfield(state, -2, "id");
    set_method(state, "set", lua_overlay_set);
    set_method(state, "addText", lua_overlay_add_text);
    set_method(state, "addButton", lua_overlay_add_button);
    set_method(state, "addCheckbox", lua_overlay_add_checkbox);
    set_method(state, "addSwitch", lua_overlay_add_switch);
    set_method(state, "addList", lua_overlay_add_list);
    set_method(state, "addOpenGLSurface", lua_overlay_add_glsurface);
    set_method(state, "addGrid", lua_overlay_add_grid);
    set_method(state, "addHLayout", lua_overlay_add_hlayout);
    set_method(state, "addVLayout", lua_overlay_add_vlayout);
    set_method(state, "addImage", lua_overlay_add_image);
    set_method(state, "addWebView", lua_overlay_add_webview);
    set_method(state, "width", lua_overlay_width);
    set_method(state, "height", lua_overlay_height);
    set_method(state, "align", lua_overlay_align);
    set_method(state, "position", lua_overlay_position);
    set_method(state, "background", lua_overlay_background);
    set_method(state, "alpha", lua_overlay_alpha);
    set_method(state, "visible", lua_overlay_visible);
    set_method(state, "clear", lua_overlay_clear);
    set_method(state, "remove", lua_overlay_remove);
}

static int lua_add_overlay(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jint id = 0;

    (void)state;
    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, "createOverlay", "()I") : NULL;
    if (method) {
        id = (*env)->CallStaticIntMethod(env, bridge, method);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        id = 0;
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    if (id <= 0) {
        lua_pushnil(state);
    } else {
        push_overlay(state, (int)id);
    }
    return 1;
}

static int lua_set_overlay(lua_State *state) {
    table_id(state, 1, "overlay");
    return lua_overlay_set(state);
}

static int lua_toast(lua_State *state) {
    const char *message = luaL_checkstring(state, 1);
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring java_message;
    int ok = 0;

    if (!env || !g_fp_runtime.context) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "showToast",
                                   "(Landroid/content/Context;Ljava/lang/String;)V")
        : NULL;
    java_message = method ? make_string(env, message) : NULL;
    if (method && java_message) {
        (*env)->CallStaticVoidMethod(env, bridge, method,
                                     g_fp_runtime.context, java_message);
        ok = 1;
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = 0;
    }
    if (java_message) {
        (*env)->DeleteLocalRef(env, java_message);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok);
    return 1;
}

static int lua_overlay(lua_State *state) {
    const char *title = luaL_optstring(state, 1, "FalconPatch");
    const char *body = luaL_optstring(state, 2, "");
    return call_static_boolean_context_string(
        state, "showOverlay",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        title, body);
}

static int lua_clear_overlay(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jboolean ok = JNI_FALSE;

    (void)state;
    if (!env) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, "clearOverlay", "()Z") : NULL;
    if (method) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int lua_inflate_xml(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring xml;
    jboolean ok = JNI_FALSE;

    if (!env || !g_fp_runtime.context) {
        lua_pushboolean(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "inflateXmlOverlay",
                                   "(Landroid/content/Context;Ljava/lang/String;)Z")
        : NULL;
    xml = method ? make_string(env, luaL_checkstring(state, 1)) : NULL;
    if (method && xml) {
        ok = (*env)->CallStaticBooleanMethod(env, bridge, method,
                                             g_fp_runtime.context, xml);
    }
    if ((*env)->ExceptionCheck(env)) {
        clear_exception(env);
        ok = JNI_FALSE;
    }
    if (xml) {
        (*env)->DeleteLocalRef(env, xml);
    }
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushboolean(state, ok == JNI_TRUE);
    return 1;
}

static int lua_inspect(lua_State *state) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jstring result;

    if (!env) {
        lua_pushnil(state);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge
        ? (*env)->GetStaticMethodID(env, bridge, "inspectCurrentUi",
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

static int call_static_int(lua_State *state, const char *name) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jint result = 0;

    if (!env) {
        lua_pushinteger(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, name, "()I") : NULL;
    if (method) {
        result = (*env)->CallStaticIntMethod(env, bridge, method);
    }
    clear_exception(env);
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushinteger(state, result);
    return 1;
}

static int call_static_float(lua_State *state, const char *name) {
    JNIEnv *env = fp_get_env();
    jclass bridge;
    jmethodID method;
    jfloat result = 0;

    if (!env) {
        lua_pushnumber(state, 0);
        return 1;
    }
    bridge = bridge_class(env);
    method = bridge ? (*env)->GetStaticMethodID(env, bridge, name, "()F") : NULL;
    if (method) {
        result = (*env)->CallStaticFloatMethod(env, bridge, method);
    }
    clear_exception(env);
    if (bridge) {
        (*env)->DeleteLocalRef(env, bridge);
    }
    lua_pushnumber(state, result);
    return 1;
}

static int lua_screen_width(lua_State *state) {
    return call_static_int(state, "screenWidth");
}

static int lua_screen_height(lua_State *state) {
    return call_static_int(state, "screenHeight");
}

static int lua_density(lua_State *state) {
    return call_static_float(state, "density");
}

static int lua_fps(lua_State *state) {
    return call_static_float(state, "refreshRate");
}

static int lua_screen_info(lua_State *state) {
    lua_newtable(state);
    lua_screen_width(state);
    lua_setfield(state, -2, "width");
    lua_screen_height(state);
    lua_setfield(state, -2, "height");
    lua_density(state);
    lua_setfield(state, -2, "density");
    lua_fps(state);
    lua_setfield(state, -2, "fps");
    return 1;
}

int fp_open_ui_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"toast", lua_toast},
        {"overlay", lua_overlay},
        {"clear_overlay", lua_clear_overlay},
        {"inflate_xml", lua_inflate_xml},
        {"inspect", lua_inspect},
        {"addOverlay", lua_add_overlay},
        {"setOverlay", lua_set_overlay},
        {"screenWidth", lua_screen_width},
        {"screenHeight", lua_screen_height},
        {"density", lua_density},
        {"fps", lua_fps},
        {"screenInfo", lua_screen_info},
        {NULL, NULL}
    };

    luaL_newlib(state, functions);
    lua_pushinteger(state, FP_UI_FILL_SCREEN_WIDTH);
    lua_setfield(state, -2, "FILL_SCREEN_WIDTH");
    lua_pushinteger(state, FP_UI_FILL_SCREEN_HEIGHT);
    lua_setfield(state, -2, "FILL_SCREEN_HEIGHT");
    lua_pushinteger(state, FP_UI_WRAP_CONTENT);
    lua_setfield(state, -2, "WRAP_CONTENT");
    lua_pushinteger(state, 48);
    lua_setfield(state, -2, "TOP");
    lua_pushinteger(state, 80);
    lua_setfield(state, -2, "BOTTOM");
    lua_pushinteger(state, 8388611);
    lua_setfield(state, -2, "START");
    lua_pushinteger(state, 8388613);
    lua_setfield(state, -2, "END");
    lua_pushinteger(state, 17);
    lua_setfield(state, -2, "CENTER");
    return 1;
}
