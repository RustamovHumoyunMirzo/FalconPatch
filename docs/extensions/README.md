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

- [`jni`](jni.md): selected Android/JNI access and `Java.use(...)` reflection
- [`app`](app.md): package, SDK, foreground, and activity state
- [`assets`](assets.md): injected asset lookup, reads, and UI handles
- [`ui`](ui.md): toast, overlays, XML loading, and view inspection
- [`gui`](gui.md): compatibility alias for `ui`
- [`events`](events.md): lifecycle and custom event polling
- [`intent`](intent.md): activity and broadcast intents

Imported APIs are available only after loading their module:

```lua
local jni = require("jni")
local ui = require("ui")
local assets = require("assets")
```

Only the small `fpatch` table is installed globally. FalconPatch does not add
imported module tables to the global namespace. Several modules provide both
snake_case and Android-style camelCase aliases for common operations so Lua
scripts can stay compact without needing global helper tables.

---

[< Detach Command](../detach.md)
