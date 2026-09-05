# Assets Module

Load the module before using it:

```lua
local assets = require("assets")
```

Files passed to `fpatch inject --asset ...` are copied into the patched APK
under `assets/falconpatch/user/<basename>`. The Lua module resolves names inside
that injected asset root, so scripts can refer to the full injected name or only
the basename.

## Lookup

| Function | Result |
| --- | --- |
| `assets.exists(name)` | `true` when the asset exists |
| `assets.size(name)` | byte size or `nil` |
| `assets.list()` | array of asset basenames currently visible under `falconpatch/user` |
| `assets.name(name)`, `assets.path(name)` | normalized Android asset path |

```lua
if assets.exists("panel.html") then
    fpatch.log(4, "panel bytes=" .. tostring(assets.size("panel.html")))
end
```

## Reading

`assets.read(name)` and `assets.text(name)` return the asset bytes as a Lua
string. On failure they return `nil, reason`.

```lua
local html, err = assets.text("panel.html")
if html then
    require("ui").addOverlay():addWebView(html)
else
    fpatch.log(5, "asset read failed: " .. err)
end
```

## UI Handles

Use these helpers when connecting injected files to UI widgets:

| Function | Use |
| --- | --- |
| `assets.url(name)`, `assets.uri(name)` | FalconPatch asset handle |
| `assets.image(name)` | alias for `assets.url(name)` for `ImageView` |
| `assets.webview(name)` | `file:///android_asset/...` URL for `WebView` |
| `assets.android_url(name)` | alias for `assets.webview(name)` |

```lua
local ui = require("ui")
local assets = require("assets")

local overlay = ui.addOverlay()
overlay:align(ui.TOP)

local image = overlay:addImage(assets.image("debug-overlay.png"))
image:width(128)
image:height(128)

local panel = overlay:addWebView("")
panel:width(ui.FILL_SCREEN_WIDTH)
panel:height(260)
panel:url(assets.webview("panel.html"))
```

Asset names cannot escape the injected root with `..` segments.

---

[< Extension index](README.md)
