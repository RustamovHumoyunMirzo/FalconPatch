# Events Module

Load the module before using it:

```lua
local events = require("events")
```

The Java bootstrap records activity lifecycle events and selected runtime
actions in a small FIFO queue.

## `poll()`

Returns the next event string, or `nil` when the queue is empty.

```lua
while true do
    local event = events.poll()
    if not event then break end
    fpatch.log(4, event)
end
```

## `drain()`

Returns all currently queued events as an array.

```lua
for _, event in ipairs(events.drain()) do
    fpatch.log(4, event)
end
```

## `emit(event)`

Adds a custom event string to the same queue and returns `true` when submitted.

```lua
events.emit("smoke:started")
```

Common event prefixes include `activity:`, `ui:`, and `intent:`.

## Element Events

Register UI element events from the events module. Pass a handle returned from
`overlay:addButton`, `overlay:addCheckbox`, `overlay:addText`, or
`overlay:addWebView`.

| Function | Trigger |
| --- | --- |
| `onClick(element, event_name)` | click/tap |
| `onDown(element, event_name)` | touch press |
| `onUp(element, event_name)` | touch release/cancel |
| `onDrag(element, event_name)` | touch move |
| `onHover(element, event_name)` | pointer hover |

The event name is optional. If omitted, FalconPatch emits a default
`ui:<kind>:<element_id>` event. Touch, drag, and hover events append coordinate
data.

```lua
local ui = require("ui")
local events = require("events")

local overlay = ui.addOverlay()
local button = overlay:addButton("Refresh")
button:setCornerRadius(8)
events.onClick(button, "debug:refresh")

for _, event in ipairs(events.drain()) do
    if event == "debug:refresh" then
        fpatch.log(4, "refresh requested")
    end
end
```

---

[< Extension index](README.md)
