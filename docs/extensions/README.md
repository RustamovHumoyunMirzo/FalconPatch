# FalconPatch Extensions

FalconPatch exposes a small global Lua API and several modules loaded with
`require(...)`. The global API is documented separately from imported modules;
each module has its own reference so new functions do not accumulate in one
shared table.

## Lua Runtime

- [Global `fpatch` API](globals.md)
- [Embedded Lua scripts and modules](lua-scripts.md)
- [Native Lua modules](native-lua-modules.md)

## Built-In Modules

- [`jni`](jni.md): selected Android and Java access
- [`gui`](gui.md): main-thread Android UI helpers
- [`background-worker`](background-worker.md): restricted detached Lua jobs

Imported APIs are available only after loading their module:

```lua
local jni = require("jni")
local gui = require("gui")
```

Only the small `fpatch` table is installed globally. FalconPatch does not add
the `jni`, `gui`, or `background-worker` tables to the global namespace.

---

[< Inject Command](../inject.md)
