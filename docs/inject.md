# Injection And Extension Reference

The full workflow and all CLI flags are documented in
[COMMANDS.md](COMMANDS.md). This page focuses on the native extension boundary.

## Extension Entry Points

Include `FalconPatch.h` from `dist/android/sdk/include`. A library may expose
either or both optional symbols:

```c
FPATCH_EXPORT int falconpatch_init(const FalconPatchHostApi *host);
FPATCH_EXPORT const FalconPatchLuaModule *falconpatch_lua_module(void);
```

A custom symbol selected with `--native-init` uses the same initializer
signature. Return `0` for success and nonzero to disable FalconPatch startup.

`FalconPatchHostApi` version 1 provides:

- `java_vm`: process `JavaVM *`
- `application_context`: global-reference Android application context
- `get_jni_env()`: obtains/attaches a `JNIEnv *` for the current thread
- `log(priority, tag, message)`: Android log writer
- `register_lua_module(name, open_function)`: add another `require` module

Do not delete `application_context`. It is owned by the runtime.

## Lua Extension Descriptor

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

Lua can then load it without adding a global:

```lua
local extension = require("myextension")
```

The extension and runtime must use the same Lua ABI. FalconPatch 1.0.0 embeds
Lua 5.4.8 and ships matching headers. Link extensions against the runtime for
their target ABI so Lua C API symbols resolve. See the native extension example
for an imported-library CMake target.

## Payload Order

At process startup FalconPatch performs this sequence:

1. Java/Kotlin bootstrap loads the runtime library named in manifest metadata.
2. Native runtime reads and validates `assets/falconpatch/runtime.bin`.
3. Restricted Lua state is created.
4. Built-in modules are registered in `package.preload`.
5. User native libraries are loaded and initialized in profile order.
6. User Lua extension descriptors and embedded Lua modules are registered.
7. Lua scripts marked `entry: true` execute in profile order.

Any initialization error is logged. The provider returns without terminating
the application process.

## Built-In Modules

- `require("jni")`: package name, SDK level, and no-argument static Java string calls
- `require("gui")`: main-thread Android toast
- `require("background-worker")`: detached restricted Lua jobs

Only `fpatch.log` and `fpatch.version` are added to the small global `fpatch`
table. Filesystem and arbitrary C-module loaders are disabled.

---

[< Command tutorial](COMMANDS.md)
