# Embedded Lua Scripts

FalconPatch supports startup scripts and require-able pure Lua modules.

## Startup Scripts

Pass `--lua-entry` to execute a script once after native and Lua modules have
been registered:

```powershell
fpatch inject --source app.apk --lua-entry scripts/startup.lua
```

Multiple entry scripts run in input or profile order.

## Require-Able Modules

Pass `--lua` to register a script in `package.preload`. Its default module name
is the filename without `.lua`; profiles can set an explicit `module` value.

```lua
local module = {}

function module.status()
    return "ready"
end

return module
```

```lua
local helper = require("helper")
fpatch.log(4, helper.status())
```

Lua files are serialized into `assets/falconpatch/runtime.bin` and loaded from
memory. They are not stored as standalone APK assets.

## Restricted Environment

The runtime opens base, package, coroutine, table, string, math, and UTF-8 Lua
libraries. It removes `dofile`, `loadfile`, `package.loadlib`, filesystem search
paths, and native searchers. Use the preloaded FalconPatch modules or embed Lua
dependencies with `--lua`.

---

[< Extension index](README.md)
