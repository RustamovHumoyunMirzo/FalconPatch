# FalconPatch

[![CI](https://github.com/RustamovHumoyunMirzo/FalconPatch/actions/workflows/ci.yml/badge.svg)](https://github.com/RustamovHumoyunMirzo/FalconPatch/actions/workflows/ci.yml)

A command-line Android testing tool for loading developer-provided native `.so` modules and Lua scripts into an authorized application.

## Preview

Build the Android artifacts first so the host executable can embed them, then build `fpatch`:

```powershell
./scripts/build_android.ps1
./scripts/build.ps1
```
## Features

### Inspect APKs

`inspect` reports manifest, signing, DEX, JNI, and ELF details. It identifies
Java/Kotlin native loads, declared native methods, JNI signatures, ABIs, imported
libraries, and FalconPatch bootstrap options. `inspect-module` narrows that
evidence to one native library and its callers, exports, and dependencies.

```powershell
fpatch inspect --source app.apk
fpatch inspect --source app.apk --ndk
fpatch inspect-module --source app.apk --target mylib
```
### Direct APK Injection And Signing

`inject` patches binary Android XML directly without requiring apktool, adds the
bootstrap/runtime and payload, aligns the APK, and signs the base plus supplied
splits with one key. It supports Java or Kotlin bootstraps, randomized runtime
library names, ABI-aware native modules, Lua entry scripts, embedded modules,
and user assets.

```powershell
fpatch inject --source app.apk --lua-entry smoke.lua
fpatch inject --profile examples/example_fp_profile.yaml
fpatch inject --source app.apk --lua-entry smoke.lua --artifacts windows-x86_64.tar.gz
```

### Declarative DEX Transformation

Profiles can apply descriptor-exact, static DEX changes before packaging:

```yaml
dex_patches:
  - target: com.example.app.Config
    method: isDebuggable()Z
    action: return_true
```

The transformer supports constant booleans, numeric zero, object/array null,
and void returns, plus verifier-safe equal-length string replacement. It
validates DEX bounds, fails when selectors match nothing, and refreshes SHA-1
and Adler-32 headers.

### Smart Native Detach And Repair

`detach` removes a named `.so` from all or selected APK ABI folders and can
optionally sign the result. `--smart-repair` also removes matching
`System.load*` and `Runtime.load*` calls and safely replaces repairable static or
registered JNI callsites using evidence collected from the removed library.

```powershell
fpatch detach --target app-fpatch.apk --so mylib --out app-detached.apk
fpatch detach --target app-fpatch.apk --so mylib --out app-detached.apk --smart-repair
```

### Extensible Android Runtime

The injected C runtime loads developer `.so` extensions, exposes a native SDK,
and executes Lua entry scripts from a bounded in-memory archive. Lua can use
`Java.use(...)` reflection for constructors, methods, private fields, object
handles, and synchronous result hooks for calls made through the reflection
bridge.

```lua
local Build = Java.use("android.os.Build")
local ui = require("ui")
local events = require("events")

fpatch.log(4, "device=" .. tostring(Build:getStatic("MODEL", "Ljava/lang/String;")))

fpatch.hookMethod("com.example.DebugInfo", "buildLabel",
    "()Ljava/lang/String;", function(_, original)
        return original .. " [patched]"
    end)

local overlay = ui.addOverlay()
overlay:width(ui.FILL_SCREEN_WIDTH)
overlay:align(ui.TOP)

local button = overlay:addButton("Run check")
button:setCornerRadius(8)
button:background(0xff1e88e5)
button:textColor(0xffffffff)
```

### In-App UI And Android APIs

Lua scripts can build transparent draggable overlays and nested horizontal,
vertical, list, and grid layouts. Built-in elements include text, buttons,
checkboxes, switches, images, WebView, and OpenGL surfaces with styling, state,
and click/touch/hover events. Separate modules expose lifecycle events, intents,
application/activity state, screen and FPS information, toast messages, XML
inflation, view inspection, and injected asset reads.

### Portable Prebuilt Artifacts

Release bundles contain the host `fpatch` executable, all Android runtime ABIs,
Java and Kotlin bootstrap DEX files, SDK headers, and checksum-verified metadata.
`--artifacts <platform>-<arch>.tar.gz` lets users inject without installing an
NDK, Java/Kotlin compiler, or native developer toolchain.

## Documentation

You can get started with the [build guide](docs/BUILD.md), or see the
[full command tutorial](docs/COMMANDS.md),
[DEX patch reference](docs/dex-patches.md),
[native extension reference](docs/inject.md), and
[Lua extension reference](docs/extensions/README.md).
