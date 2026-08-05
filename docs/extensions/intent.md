# Intent Module

Load the module before using it:

```lua
local intent = require("intent")
```

Intent helpers return `true` when Android accepted the request and `false` when
context, action, or dispatch failed.

## `start_activity(action, uri, package_name)`

Starts an activity with `FLAG_ACTIVITY_NEW_TASK`. `uri` and `package_name` are
optional strings.

```lua
intent.start_activity("android.intent.action.VIEW", "https://example.com")
```

## `broadcast(action, uri, package_name)`

Sends a broadcast intent. `uri` and `package_name` are optional strings.

```lua
intent.broadcast("com.example.DEBUG_REFRESH")
```

Keep intent actions explicit and scoped to apps you own or are authorized to
test.

---

[< Extension index](README.md)
