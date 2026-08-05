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
- [`app`](app.md): package, SDK, foreground, and activity state
- [`ui`](ui.md): toast, overlays, XML loading, and view inspection
- [`gui`](gui.md): compatibility alias for `ui`
- [`events`](events.md): lifecycle and custom event polling
- [`intent`](intent.md): activity and broadcast intents
- [`background-worker`](background-worker.md): restricted detached Lua jobs

Imported APIs are available only after loading their module:

```lua
local jni = require("jni")
local ui = require("ui")
```

Only the small `fpatch` table is installed globally. FalconPatch does not add
imported module tables to the global namespace.

---

[< Inject Command](../inject.md)
