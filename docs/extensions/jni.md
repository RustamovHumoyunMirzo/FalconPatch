# JNI Module

Load the module before using it:

```lua
local jni = require("jni")
```

The module intentionally exposes a narrow JNI surface. Java exceptions are
cleared and converted to Lua failure results instead of escaping into the app.

## `Java.use(class_name)`

`Java.use(...)` is installed globally for quick reflection work, and the same
factory is also available as `require("jni").use(...)`. It resolves classes
through the app class loader when possible, can call private members via Java
reflection, and converts Java failures into `nil, error`.

```lua
local Build = Java.use("android.os.Build")
fpatch.log(4, "device=" .. tostring(Build:getStatic("MODEL", "Ljava/lang/String;")))

local DebugInfo = Java.use("com.example.DebugInfo")
if DebugInfo:exists() then
    local label, err = DebugInfo:callStatic(
        "buildLabel",
        "(Ljava/lang/String;I)Ljava/lang/String;",
        "local",
        42)
    if label then
        fpatch.log(4, label)
    else
        fpatch.log(6, err)
    end
end
```

Class handles:

| Method | Result |
| --- | --- |
| `exists()` | `true` when the class resolves |
| `callStatic(name, signature, ...)` | reflected static method result |
| `getStatic(name, signature)` | static field value |
| `setStatic(name, signature, value)` | `true` or `nil, error` |
| `new(signature, ...)` | reflected object handle |

Object handles:

| Method | Result |
| --- | --- |
| `call(name, signature, ...)` | reflected instance method result |
| `get(name, signature)` | instance field value |
| `set(name, signature, value)` | `true` or `nil, error` |
| `release()` | drops FalconPatch's stored object reference |
| `toString()` | local Lua handle label |

Use JVM descriptors for signatures: `I` for `int`, `Z` for `boolean`,
`J` for `long`, `D` for `double`, `Ljava/lang/String;` for strings, and method
forms such as `(Ljava/lang/String;)Z`. Primitive values, strings, booleans, and
other reflected object handles can be passed as arguments.

```lua
local Formatter = Java.use("java.text.SimpleDateFormat")
local fmt = Formatter:new("(Ljava/lang/String;)V", "yyyy-MM-dd")
local date = Java.use("java.util.Date"):new("()V")
local text = fmt:call("format", "(Ljava/util/Date;)Ljava/lang/String;", date)
date:release()
fmt:release()
```

Calls made with `callStatic(...)` and object `call(...)` pass through registered
[`fpatch.hookMethod(...)`](method-hooks.md) callbacks before their result is
returned to Lua.

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

For methods with parameters, instance methods, private members, or object
handles, prefer `Java.use(...)`.

---

[< Extension index](README.md)
