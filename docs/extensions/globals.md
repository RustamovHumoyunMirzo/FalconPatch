# Global Lua API

The `fpatch` table is available without `require(...)`. These are the only
FalconPatch functions installed globally.

| Function | Parameters | Result |
| --- | --- | --- |
| `fpatch.version()` | none | FalconPatch runtime version string |
| `fpatch.abi()` | none | runtime Android ABI string |
| `fpatch.now_ms()` | none | Unix epoch time in milliseconds |
| `fpatch.monotonic_ms()` | none | monotonic clock in milliseconds |
| `fpatch.runtime()` | none | table with `version`, `abi`, `started`, `records`, and `assets` |
| `fpatch.try(function, ...)` | Lua function plus optional arguments | `true, ...` on success or `false, error` |
| `fpatch.log(priority, message)` | Android log priority integer and message string | no values |

## Example

```lua
fpatch.log(4, "runtime " .. fpatch.version() .. " started")

local ok, result = fpatch.try(function()
    return "abi=" .. fpatch.abi()
end)
if ok then
    fpatch.log(4, result)
end
```

Android log priorities commonly used by scripts are `3` for debug, `4` for
info, `5` for warning, and `6` for error.

Keep larger surfaces in their own modules with `require("ui")`,
`require("events")`, `require("assets")`, `require("app")`,
`require("intent")`, or `require("jni")`.

---

[< Extension index](README.md)
