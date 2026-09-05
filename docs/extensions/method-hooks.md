# Method Hooks

FalconPatch 1.9.0 can intercept reflected Java method calls inside the patched
process. Register a hook before making the matching call through `Java.use(...)`:

```lua
local hook = fpatch.hookMethod(
    "com.example.app.User",
    "getRole",
    "()Ljava/lang/String;",
    function(this, originalResult)
        return "ADMIN"
    end)

local User = Java.use("com.example.app.User")
local user = User:new("()V")
fpatch.log(4, user:call("getRole", "()Ljava/lang/String;"))
```

The callback receives the reflected object handle as `this` for instance calls,
or `nil` for static calls, followed by the original return value. Returning a
non-`nil` value replaces that value. Returning `nil`, raising an error, or
recursively calling the same hooked method preserves the current value. Multiple
matching hooks run in registration order, so each callback receives the value
produced by the previous hook.

Omit the signature to match every overload with the method name:

```lua
local hook = fpatch.hookMethod("com.example.DebugInfo", "label", function(_, value)
    return "test:" .. tostring(value)
end)
```

Using an exact JVM signature is recommended for overloaded methods. The class
name can use dotted or slash notation.

## Hook Handles

| Method | Result |
| --- | --- |
| `hook:disable()` | disables the hook and returns whether it still exists |
| `hook:enable()` | enables the hook and returns whether it still exists |
| `hook:isEnabled()` | current enabled state |
| `hook:stats()` | `{ calls, failures, enabled }`, or `nil` after removal |
| `hook:remove()` | permanently unregisters the hook, including inside its callback |

FalconPatch stores at most 128 hooks. Remove hooks that are no longer needed so
their Lua callbacks can be released.
Hooks added during a callback begin matching on the next reflected call.

## Capabilities and Scope

```lua
local capabilities = fpatch.hookCapabilities()
fpatch.log(4, capabilities.backend)
```

The 1.9.0 backend is `reflection-bridge`: it intercepts `Java.use(...):call(...)`
and `Java.use(...):callStatic(...)` operations. It does not intercept arbitrary
calls made directly by the application's compiled Java/Kotlin code. Check
`directArtMethod` before relying on a future direct ART backend; it is `false`
in this release.

FalconPatch deliberately does not infer private `ArtMethod` field offsets. Those
layouts vary by Android release, runtime build, architecture, and OEM, and a bad
write can corrupt the process before a hook can report an error.

---

[< Extension index](README.md)
