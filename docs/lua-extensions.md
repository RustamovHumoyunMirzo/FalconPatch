# Lua Extensions

FalconPatch supports pure Lua files embedded in the payload and native `.so`
extensions that expose a Lua module. Injection flags and profiles are covered in
[COMMANDS.md](COMMANDS.md); this page covers the Lua runtime and extension API.

## Pure Lua Modules

Pass a startup script with `--lua-entry`. Pass a require-able module with
`--lua`; its default module name is the filename without `.lua`:

```powershell
fpatch inject --source app.apk \
  --lua scripts/helper.lua \
  --lua-entry scripts/startup.lua
```

Profiles can assign a stable `module` name and choose whether each script is an
entry point. Lua files are serialized into `assets/falconpatch/runtime.bin` and
loaded from memory rather than exposed as standalone APK assets.

## Native Lua Modules

Build against `FalconPatch.h` and the matching Lua headers from
`dist/android/sdk/include`:

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

Lua loads the extension without adding a global:

```lua
local extension = require("myextension")
```

The extension and runtime must use the same Lua ABI. FalconPatch 1.0.0 embeds
Lua 5.4.8 and ships the matching headers. Link the extension against the
FalconPatch runtime for each target ABI so Lua C API symbols resolve. The
[`examples/native-extension`](../examples/native-extension) CMake project shows
the imported-library setup.

## Built-In API

```lua
local jni = require("jni")
local gui = require("gui")
local worker = require("background-worker")
```

| Function | Parameters | Result |
| --- | --- | --- |
| `fpatch.version()` | none | runtime version string |
| `fpatch.log(priority, message)` | Android log priority integer, string | none |
| `jni.package_name()` | none | package string or `nil` |
| `jni.sdk_int()` | none | Android SDK integer or `nil` |
| `jni.call_static_string(class, method)` | dotted Java class and no-arg static method | string, or `nil, error` |
| `gui.toast(message)` | string | boolean queued status |
| `worker.submit(source)` | Lua source string | boolean thread-start status |

Background jobs run in a separate restricted Lua state. Pass source text rather
than a closure because Lua states cannot share closures or stack values.

## Runtime Restrictions

The runtime opens base, package, coroutine, table, string, math, and UTF-8 Lua
libraries. It removes `dofile`, `loadfile`, `package.loadlib`, filesystem search
paths, and native searchers. FalconPatch APIs are modules, not globals, except
for the small `fpatch` table and Lua's normal `require` function.

At startup, built-in modules are registered before user native modules and pure
Lua modules. Entry scripts run last, in profile order. An extension error is
logged and disables FalconPatch startup without terminating the host app.

---

[< Native extension reference](inject.md) | [Command tutorial](COMMANDS.md)
