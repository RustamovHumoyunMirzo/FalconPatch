#include "fp_internal.h"

#include <android/asset_manager.h>
#include <lauxlib.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FP_ASSET_ROOT "falconpatch/user"
#define FP_ASSET_HANDLE_PREFIX "fpatch-asset://"
#define FP_ANDROID_ASSET_PREFIX "file:///android_asset/"

static const char *skip_prefix(const char *value, const char *prefix) {
    size_t size = strlen(prefix);

    return strncmp(value, prefix, size) == 0 ? value + size : NULL;
}

static int contains_parent_segment(const char *value) {
    const char *cursor = value;

    if (!value || strstr(value, "..") == NULL) {
        return 0;
    }
    while (*cursor) {
        while (*cursor == '/' || *cursor == '\\') {
            cursor++;
        }
        if (cursor[0] == '.' && cursor[1] == '.' &&
            (cursor[2] == '\0' || cursor[2] == '/' || cursor[2] == '\\')) {
            return 1;
        }
        while (*cursor && *cursor != '/' && *cursor != '\\') {
            cursor++;
        }
    }
    return 0;
}

static int normalize_asset_name(lua_State *state, int index,
                                char *path, size_t path_size) {
    const char *name = luaL_checkstring(state, index);
    const char *relative = NULL;
    size_t i;
    int written;

    if (skip_prefix(name, FP_ASSET_HANDLE_PREFIX)) {
        relative = skip_prefix(name, FP_ASSET_HANDLE_PREFIX);
    } else if (skip_prefix(name, "asset://")) {
        relative = skip_prefix(name, "asset://");
    } else if (skip_prefix(name, FP_ANDROID_ASSET_PREFIX)) {
        relative = skip_prefix(name, FP_ANDROID_ASSET_PREFIX);
    } else if (skip_prefix(name, "assets/" FP_ASSET_ROOT "/")) {
        relative = skip_prefix(name, "assets/" FP_ASSET_ROOT "/");
    } else if (skip_prefix(name, FP_ASSET_ROOT "/")) {
        relative = skip_prefix(name, FP_ASSET_ROOT "/");
    } else {
        relative = name;
    }
    while (*relative == '/' || *relative == '\\') {
        relative++;
    }
    if (!relative[0] || contains_parent_segment(relative)) {
        return luaL_error(state, "invalid asset name");
    }
    written = snprintf(path, path_size, FP_ASSET_ROOT "/%s", relative);
    if (written < 0 || (size_t)written >= path_size) {
        return luaL_error(state, "asset name is too long");
    }
    for (i = 0; path[i]; i++) {
        if (path[i] == '\\') {
            path[i] = '/';
        }
    }
    return 1;
}

static AAsset *open_asset(lua_State *state, int index,
                          char *path, size_t path_size) {
    normalize_asset_name(state, index, path, path_size);
    if (!g_fp_runtime.asset_manager) {
        return NULL;
    }
    return AAssetManager_open(g_fp_runtime.asset_manager, path, AASSET_MODE_BUFFER);
}

static int lua_exists(lua_State *state) {
    char path[512];
    AAsset *asset = open_asset(state, 1, path, sizeof(path));

    if (asset) {
        AAsset_close(asset);
    }
    lua_pushboolean(state, asset != NULL);
    return 1;
}

static int lua_read(lua_State *state) {
    char path[512];
    AAsset *asset = open_asset(state, 1, path, sizeof(path));
    off_t length;
    char *buffer;
    int read_count;

    if (!asset) {
        lua_pushnil(state);
        lua_pushliteral(state, "asset not found");
        return 2;
    }
    length = AAsset_getLength(asset);
    if (length < 0 || (uint64_t)length > SIZE_MAX || length > INT_MAX) {
        AAsset_close(asset);
        lua_pushnil(state);
        lua_pushliteral(state, "asset is too large");
        return 2;
    }
    buffer = (char *)malloc((size_t)length);
    if (!buffer && length > 0) {
        AAsset_close(asset);
        lua_pushnil(state);
        lua_pushliteral(state, "out of memory");
        return 2;
    }
    read_count = length > 0 ? AAsset_read(asset, buffer, (size_t)length) : 0;
    AAsset_close(asset);
    if (read_count != length) {
        free(buffer);
        lua_pushnil(state);
        lua_pushliteral(state, "asset read failed");
        return 2;
    }
    lua_pushlstring(state, buffer ? buffer : "", (size_t)length);
    free(buffer);
    return 1;
}

static int lua_size(lua_State *state) {
    char path[512];
    AAsset *asset = open_asset(state, 1, path, sizeof(path));
    off_t length;

    if (!asset) {
        lua_pushnil(state);
        return 1;
    }
    length = AAsset_getLength(asset);
    AAsset_close(asset);
    if (length < 0 || (uint64_t)length > SIZE_MAX) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, (lua_Integer)length);
    }
    return 1;
}

static int lua_name(lua_State *state) {
    char path[512];

    normalize_asset_name(state, 1, path, sizeof(path));
    lua_pushstring(state, path);
    return 1;
}

static int lua_url(lua_State *state) {
    char path[512];

    normalize_asset_name(state, 1, path, sizeof(path));
    lua_pushfstring(state, FP_ASSET_HANDLE_PREFIX "%s", path);
    return 1;
}

static int lua_android_url(lua_State *state) {
    char path[512];

    normalize_asset_name(state, 1, path, sizeof(path));
    lua_pushfstring(state, FP_ANDROID_ASSET_PREFIX "%s", path);
    return 1;
}

static int lua_list(lua_State *state) {
    AAssetDir *dir;
    const char *file;
    int index = 1;

    lua_newtable(state);
    if (!g_fp_runtime.asset_manager) {
        return 1;
    }
    dir = AAssetManager_openDir(g_fp_runtime.asset_manager, FP_ASSET_ROOT);
    if (!dir) {
        return 1;
    }
    while ((file = AAssetDir_getNextFileName(dir)) != NULL) {
        lua_pushstring(state, file);
        lua_seti(state, -2, index++);
    }
    AAssetDir_close(dir);
    return 1;
}

int fp_open_assets_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"exists", lua_exists},
        {"read", lua_read},
        {"text", lua_read},
        {"size", lua_size},
        {"name", lua_name},
        {"path", lua_name},
        {"url", lua_url},
        {"uri", lua_url},
        {"image", lua_url},
        {"webview", lua_android_url},
        {"android_url", lua_android_url},
        {"list", lua_list},
        {NULL, NULL}
    };

    luaL_newlib(state, functions);
    lua_pushliteral(state, FP_ASSET_ROOT);
    lua_setfield(state, -2, "ROOT");
    lua_pushliteral(state, FP_ASSET_HANDLE_PREFIX);
    lua_setfield(state, -2, "HANDLE_PREFIX");
    lua_pushliteral(state, FP_ANDROID_ASSET_PREFIX);
    lua_setfield(state, -2, "ANDROID_ASSET_PREFIX");
    return 1;
}
