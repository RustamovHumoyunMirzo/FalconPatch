# GUI Module

`gui` is kept as a compatibility alias for [`ui`](ui.md). Existing scripts can
continue to load it:

```lua
local gui = require("gui")
```

New scripts should prefer:

```lua
local ui = require("ui")
```

The returned table currently exposes the same functions as `ui`, including
`toast`, `overlay`, `clear_overlay`, `inflate_xml`, and `inspect`.

## `toast(message)`

Queues an Android toast through the Java bootstrap and returns `true` when the
request was submitted. It returns `false` when JNI, the application context, or
the Java bridge is unavailable, or when Java raises an exception.

```lua
if not gui.toast("FalconPatch loaded") then
    fpatch.log(5, "toast could not be shown")
end
```

The message must be a Lua string. UI dispatch is handled by the bootstrap, so
scripts do not need to switch to Android's main thread themselves.

---

[< Extension index](README.md)
