# App Module

Load the module before using it:

```lua
local app = require("app")
```

## `package_name()`

Returns the patched app package name, or `nil` when Android context is
unavailable.

## `sdk_int()`

Returns `android.os.Build.VERSION.SDK_INT`, or `nil` when JNI is unavailable.

## `current_activity()`

Returns the class name of the foreground activity tracked by the bootstrap, or
`nil` when no activity is resumed.

## `foreground()`

Returns `true` when a foreground activity is known.

```lua
if app.foreground() then
    fpatch.log(4, "activity: " .. (app.current_activity() or "unknown"))
end
```

---

[< Extension index](README.md)
