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

## `class_exists(class_name)`

Returns `true` when the class can be resolved.

```lua
if jni.class_exists("com.example.DebugInfo") then
    fpatch.log(4, "debug class is available")
end
```

## Static Fields

Read selected static fields:

| Function | Java signature |
| --- | --- |
| `get_static_string(class_name, field_name)` | `Ljava/lang/String;` |
| `get_static_int(class_name, field_name)` | `I` |
| `get_static_boolean(class_name, field_name)` | `Z` |

Each returns `nil` when the class, field, JNI environment, or type signature is
unavailable.

## Static Methods

Call selected no-argument static methods:

| Function | Java signature |
| --- | --- |
| `call_static_string(class_name, method_name)` | `()Ljava/lang/String;` |
| `call_static_int(class_name, method_name)` | `()I` |
| `call_static_boolean(class_name, method_name)` | `()Z` |

On failure, call helpers return `nil, error`.

---

[< Extension index](README.md)
