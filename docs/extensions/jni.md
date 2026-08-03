# JNI Module

Load the module before using it:

```lua
local jni = require("jni")
```

The module intentionally exposes a narrow JNI surface. Java exceptions are
cleared and converted to Lua failure results instead of escaping into the app.

## `package_name()`

Returns the Android application package name as a string. Returns `nil` when
the application context or JNI environment is unavailable.

```lua
local package_name = jni.package_name()
```

## `sdk_int()`

Returns `android.os.Build.VERSION.SDK_INT` as an integer. Returns `nil` when JNI
or the field is unavailable.

```lua
if (jni.sdk_int() or 0) >= 33 then
    fpatch.log(4, "running on Android 13 or newer")
end
```

## `call_static_string(class_name, method_name)`

Calls a no-argument static Java method with the signature
`()Ljava/lang/String;`. `class_name` may use dotted Java notation. On success it
returns the string, or `nil` when Java returns `null`. On a JNI failure it
returns `nil, error`.

```lua
local value, err = jni.call_static_string("com.example.DebugInfo", "buildLabel")
if err then
    fpatch.log(6, err)
end
```

This function does not support arguments, instance methods, or other return
types.

---

[< Extension index](README.md)
