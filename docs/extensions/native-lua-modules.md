# Native Lua Modules

A developer-provided `.so` can expose a Lua module descriptor alongside its
optional FalconPatch initializer. Build against `FalconPatch.h` and the matching
Lua headers from `dist/android/sdk/include`.

```c
#include "FalconPatch.h"
#include <lauxlib.h>

static int open_module(lua_State *state) {
    lua_newtable(state);
    return 1;
}

FPATCH_EXPORT const FalconPatchLuaModule *falconpatch_lua_module(void) {
    static const FalconPatchLuaModule module = {
        FPATCH_EXTENSION_ABI,
        "myextension",
        open_module
    };
    return &module;
}
```

Load the registered module normally:

```lua
local extension = require("myextension")
```

The extension and runtime must use the same Lua ABI. FalconPatch 1.0.0 embeds
Lua 5.4.8 and ships matching headers. Link the extension against the
FalconPatch runtime for every target ABI so Lua C API symbols resolve.

The [`examples/native-extension`](../../examples/native-extension) project
shows the imported-library CMake setup. Use `--native-module` or the profile
`module` field when the injected name should override the descriptor name.

---

[< Extension index](README.md)
