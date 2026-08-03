# Background Worker Module

Load the module before using it:

```lua
local worker = require("background-worker")
```

## `submit(source)`

Starts a detached thread with a new restricted Lua state and returns `true` if
the thread was created. It returns `false` if FalconPatch cannot allocate the
job or start the thread.

```lua
local started = worker.submit([[
    fpatch.log(4, "background work started")
]])
```

Pass Lua source text, not a function or closure. Lua states cannot share stack
values or closures. The worker state receives the global `fpatch` table and the
same preloaded modules, but it does not share globals created by the submitting
script. Runtime errors are written to Android logcat.

---

[< Extension index](README.md)
