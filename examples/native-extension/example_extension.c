#include "FalconPatch.h"

#include <lauxlib.h>

static int lua_greeting(lua_State *state) {
    lua_pushliteral(state, "Hello from a FalconPatch native extension");
    return 1;
}

static int open_example(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"greeting", lua_greeting},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}

FPATCH_EXPORT const FalconPatchLuaModule *falconpatch_lua_module(void) {
    static const FalconPatchLuaModule module = {
        FPATCH_EXTENSION_ABI,
        "example",
        open_example
    };
    return &module;
}
