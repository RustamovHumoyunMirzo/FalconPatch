# UI Module

Load the module before using it:

```lua
local ui = require("ui")
```

UI work is dispatched through the Java bootstrap on Android's main thread.
Functions return `false` when no foreground activity is available or the bridge
cannot submit the request.

## Constants

| Constant | Use |
| --- | --- |
| `ui.FILL_SCREEN_WIDTH` | full screen overlay or element width |
| `ui.FILL_SCREEN_HEIGHT` | full screen overlay or element height |
| `ui.WRAP_CONTENT` | Android `wrap_content` sizing |
| `ui.TOP`, `ui.BOTTOM`, `ui.START`, `ui.END`, `ui.CENTER` | overlay gravity |

## Screen Getters

```lua
local info = ui.screenInfo()
fpatch.log(4, ("screen=%dx%d density=%.2f fps=%.1f")
    :format(info.width, info.height, info.density, info.fps))
```

Individual getters are also available: `screenWidth()`, `screenHeight()`,
`density()`, and `fps()`.

## `addOverlay()`

Creates a transparent `FrameLayout` overlay attached to the current activity and
returns an overlay handle. It returns `nil` if no activity is foreground.

```lua
local overlay = ui.addOverlay()
overlay:width(ui.FILL_SCREEN_WIDTH)
overlay:height(ui.WRAP_CONTENT)
overlay:align(ui.TOP)
overlay:alpha(90)
```

Overlay methods:

| Method | Effect |
| --- | --- |
| `set(key, number)` | generic numeric setter |
| `width(value)`, `height(value)` | set layout size |
| `position(x, y)` | set margins in dp |
| `align(gravity)` | set Android gravity |
| `background(color)` | set ARGB integer background |
| `alpha(percent)` | set opacity from `0` to `100` |
| `visible(boolean)` | show or hide |
| `clear()` | remove all child elements |
| `remove()` | detach the overlay |

## Elements

Overlay and layout handles create child elements:

```lua
local button = overlay:addButton("Run check")
button:width(ui.WRAP_CONTENT)
button:padding(10)
button:background(0xff1e88e5)
button:textColor(0xffffffff)
button:setCornerRadius(8)
button:stroke(1, 0xffffffff)
button:draggable(true)

local row = overlay:addHLayout()
row:width(ui.FILL_SCREEN_WIDTH)

local enabled = row:addSwitch("Enabled")
enabled:weight(1.0)

local checkbox = row:addCheckbox("Verbose")
checkbox:setChecked(true)
checkbox:weight(1.0)

local web = overlay:addWebView("<b>FalconPatch</b>")
web:width(ui.FILL_SCREEN_WIDTH)
web:height(220)
```

Element factories:

| Method | Android view |
| --- | --- |
| `addText(text)` | `TextView` |
| `addButton(text)` | `Button` |
| `addCheckbox(text)` | `CheckBox` |
| `addSwitch(text)` | `Switch` |
| `addList(items)` | `ListView`; newline or comma separated items |
| `addOpenGLSurface()` | `GLSurfaceView` |
| `addGrid()` | `GridLayout` |
| `addHLayout()`, `addVLayout()` | horizontal or vertical `LinearLayout` |
| `addImage(uri_or_data_url)` | `ImageView` |
| `addWebView(html)` | `WebView` |

Element methods:

| Method | Effect |
| --- | --- |
| `set(key, value)` | generic setter for strings, numbers, and booleans |
| `text(value)`, `setText(value)`, `getText()` | text getter/setter |
| `checked(value)`, `setChecked(value)`, `isChecked()` | checkbox state |
| `enabled(value)`, `visible(value)` | state setters/getters |
| `width(value)`, `height(value)`, `position(x, y)` | layout |
| `background(color)`, `textColor(color)`, `textSize(sp)` | styling |
| `setCornerRadius(dp)`, `stroke(width_dp, color)`, `padding(dp)` | shape and spacing |
| `weight(value)` | setter/getter for children inside horizontal or vertical layouts |
| `draggable(boolean)` | allow touch-dragging |
| `items(value)` | replace `ListView` items |
| `columns(count)` | set `GridLayout` column count |
| `image(uri_or_data_url)` | replace `ImageView` content |
| `html(value)`, `url(value)` | WebView content |

## `toast(message)`

Shows a short Android toast.

```lua
ui.toast("FalconPatch loaded")
```

## `overlay(title, body)`

Adds a lightweight diagnostic overlay to the current activity. This is a
compatibility convenience wrapper; use `addOverlay()` for custom UI trees.

```lua
ui.overlay("FalconPatch", "runtime ready")
```

## `clear_overlay()`

Removes the active FalconPatch overlay if one was added.

```lua
ui.clear_overlay()
```

## `inflate_xml(xml)`

Inflates a small Android XML view and attaches it as an overlay to the current
activity. Prefer platform widgets and inline attributes that do not depend on
private app resources.

```lua
ui.inflate_xml([[
<TextView xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="wrap_content"
    android:padding="12dp"
    android:background="#cc20242a"
    android:textColor="#ffffffff"
    android:text="FalconPatch XML overlay" />
]])
```

## `inspect()`

Returns a text snapshot of the current activity view tree.

```lua
fpatch.log(4, ui.inspect() or "no activity")
```

---

[< Extension index](README.md)
