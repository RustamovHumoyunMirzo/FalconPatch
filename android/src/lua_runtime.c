#include "fp_internal.h"

#include <android/log.h>
#include <lauxlib.h>
#include <lualib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

static void add_preload(lua_State *state, const char *name, lua_CFunction open_function) {
    luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushcfunction(state, open_function);
    lua_setfield(state, -2, name);
    lua_pop(state, 1);
}

int fp_lua_register_module(const char *name, lua_CFunction open_function) {
    size_t i;
    FpLuaModule *module;

    if (!name || !name[0] || !open_function) {
        return 0;
    }
    for (i = 0; i < g_fp_runtime.lua_module_count; i++) {
        if (strcmp(g_fp_runtime.lua_modules[i].name, name) == 0) {
            return 0;
        }
    }
    if (g_fp_runtime.lua_module_count >= FPATCH_MAX_LUA_MODULES ||
        strlen(name) >= sizeof(g_fp_runtime.lua_modules[0].name)) {
        return 0;
    }
    module = &g_fp_runtime.lua_modules[g_fp_runtime.lua_module_count++];
    snprintf(module->name, sizeof(module->name), "%s", name);
    module->open_function = open_function;
    return 1;
}

void fp_lua_register_all_modules(lua_State *state) {
    size_t i;

    for (i = 0; i < g_fp_runtime.lua_module_count; i++) {
        add_preload(state, g_fp_runtime.lua_modules[i].name,
                    g_fp_runtime.lua_modules[i].open_function);
    }
}

static int lua_host_log(lua_State *state) {
    int priority = (int)luaL_optinteger(state, 1, ANDROID_LOG_INFO);
    const char *message = luaL_checkstring(state, 2);
    fp_log(priority, "FalconPatch/Lua", message);
    return 0;
}

static int lua_host_version(lua_State *state) {
#ifdef FPATCH_ANDROID_VERSION
    lua_pushstring(state, FPATCH_ANDROID_VERSION);
#else
    lua_pushliteral(state, "1.9.0");
#endif
    return 1;
}

static int lua_host_abi(lua_State *state) {
#ifdef FPATCH_ANDROID_ABI
    lua_pushstring(state, FPATCH_ANDROID_ABI);
#else
    lua_pushliteral(state, "unknown");
#endif
    return 1;
}

static int lua_host_now_ms(lua_State *state) {
    struct timespec spec;

    if (clock_gettime(CLOCK_REALTIME, &spec) != 0) {
        lua_pushinteger(state, 0);
        return 1;
    }
    lua_pushinteger(state, (lua_Integer)spec.tv_sec * 1000 +
                           (lua_Integer)(spec.tv_nsec / 1000000));
    return 1;
}

static int lua_host_monotonic_ms(lua_State *state) {
    struct timespec spec;

    if (clock_gettime(CLOCK_MONOTONIC, &spec) != 0) {
        lua_pushinteger(state, 0);
        return 1;
    }
    lua_pushinteger(state, (lua_Integer)spec.tv_sec * 1000 +
                           (lua_Integer)(spec.tv_nsec / 1000000));
    return 1;
}

static int lua_host_runtime(lua_State *state) {
    lua_newtable(state);
    lua_host_version(state);
    lua_setfield(state, -2, "version");
    lua_host_abi(state);
    lua_setfield(state, -2, "abi");
    lua_pushboolean(state, g_fp_runtime.started);
    lua_setfield(state, -2, "started");
    lua_pushinteger(state, (lua_Integer)g_fp_runtime.archive.record_count);
    lua_setfield(state, -2, "records");
    lua_pushboolean(state, g_fp_runtime.asset_manager != NULL);
    lua_setfield(state, -2, "assets");
    return 1;
}

static int lua_host_try(lua_State *state) {
    int top = lua_gettop(state);
    int nargs = top - 1;
    int status;

    luaL_checktype(state, 1, LUA_TFUNCTION);
    lua_pushvalue(state, 1);
    if (nargs > 0) {
        int i;
        for (i = 0; i < nargs; i++) {
            lua_pushvalue(state, 2 + i);
        }
    }
    status = lua_pcall(state, nargs, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        lua_pushboolean(state, 0);
        lua_insert(state, -2);
        return 2;
    }
    {
        int results = lua_gettop(state) - top;
        lua_pushboolean(state, 1);
        lua_insert(state, top + 1);
        return results + 1;
    }
}

static void restrict_package(lua_State *state) {
    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    lua_pushliteral(state, "");
    lua_setfield(state, -2, "path");
    lua_pushliteral(state, "");
    lua_setfield(state, -2, "cpath");
    lua_pushnil(state);
    lua_setfield(state, -2, "loadlib");

    lua_getfield(state, -1, "searchers");
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        lua_seti(state, -2, 2);
        lua_pushnil(state);
        lua_seti(state, -2, 3);
        lua_pushnil(state);
        lua_seti(state, -2, 4);
    }
    lua_pop(state, 2);
}

int fp_lua_open_restricted(lua_State *state) {
    if (!state) {
        return 0;
    }
    luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_LOADLIBNAME, luaopen_package, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state, 1);

    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
    restrict_package(state);

    lua_newtable(state);
    lua_pushcfunction(state, lua_host_log);
    lua_setfield(state, -2, "log");
    lua_pushcfunction(state, lua_host_version);
    lua_setfield(state, -2, "version");
    lua_pushcfunction(state, lua_host_abi);
    lua_setfield(state, -2, "abi");
    lua_pushcfunction(state, lua_host_now_ms);
    lua_setfield(state, -2, "now_ms");
    lua_pushcfunction(state, lua_host_monotonic_ms);
    lua_setfield(state, -2, "monotonic_ms");
    lua_pushcfunction(state, lua_host_runtime);
    lua_setfield(state, -2, "runtime");
    lua_pushcfunction(state, lua_host_try);
    lua_setfield(state, -2, "try");
    lua_setglobal(state, "fpatch");
    fp_lua_install_java_global(state);
    fp_lua_install_method_hook_globals(state);
    return 1;
}

static int embedded_lua_loader(lua_State *state) {
    const FpArchiveRecord *record =
        (const FpArchiveRecord *)lua_touserdata(state, lua_upvalueindex(1));
    const char *module_name = lua_tostring(state, 1);

    if (!record || luaL_loadbuffer(state, (const char *)record->data,
                                   record->size, record->name) != LUA_OK) {
        return lua_error(state);
    }
    lua_pushstring(state, module_name ? module_name : record->name);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        return lua_error(state);
    }
    return 1;
}

static void derive_module_name(const FpArchiveRecord *record, char *name, size_t size) {
    size_t i;
    const char *source = record->aux && record->aux[0] ? record->aux : record->name;

    snprintf(name, size, "%s", source ? source : "script");
    for (i = 0; name[i]; i++) {
        if (name[i] == '/' || name[i] == '\\') {
            name[i] = '.';
        }
    }
    if (i > 4 && strcmp(name + i - 4, ".lua") == 0) {
        name[i - 4] = '\0';
    }
}

int fp_lua_run_archive(lua_State *state, const FpArchive *archive) {
    size_t i;

    for (i = 0; i < archive->record_count; i++) {
        const FpArchiveRecord *record = &archive->records[i];
        char module_name[256];
        if (record->type != FPATCH_RECORD_LUA ||
            (record->flags & FPATCH_RECORD_ENTRY) != 0) {
            continue;
        }
        derive_module_name(record, module_name, sizeof(module_name));
        luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
        lua_pushlightuserdata(state, (void *)record);
        lua_pushcclosure(state, embedded_lua_loader, 1);
        lua_setfield(state, -2, module_name);
        lua_pop(state, 1);
    }

    for (i = 0; i < archive->record_count; i++) {
        const FpArchiveRecord *record = &archive->records[i];
        if (record->type != FPATCH_RECORD_LUA ||
            (record->flags & FPATCH_RECORD_ENTRY) == 0) {
            continue;
        }
        if (luaL_loadbuffer(state, (const char *)record->data,
                            record->size, record->name) != LUA_OK ||
            lua_pcall(state, 0, 0, 0) != LUA_OK) {
            fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Lua", "%s: %s",
                    record->name, lua_tostring(state, -1));
            lua_pop(state, 1);
            return 0;
        }
    }
    return 1;
}
