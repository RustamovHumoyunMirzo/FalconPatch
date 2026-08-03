# GUI Module

Load the module before using it:

```lua
local gui = require("gui")
```

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
