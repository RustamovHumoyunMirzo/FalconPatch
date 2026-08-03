# Global Lua API

The `fpatch` table is available without `require(...)`. These are the only
FalconPatch functions installed globally.

| Function | Parameters | Result |
| --- | --- | --- |
| `fpatch.version()` | none | FalconPatch runtime version string |
| `fpatch.log(priority, message)` | Android log priority integer and message string | no values |

## Example

```lua
fpatch.log(4, "runtime " .. fpatch.version() .. " started")
```

Android log priorities commonly used by scripts are `3` for debug, `4` for
info, `5` for warning, and `6` for error.

---

[< Extension index](README.md)
