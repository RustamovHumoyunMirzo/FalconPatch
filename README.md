# FalconPatch

A command-line Android testing tool for loading developer-provided native `.so`
modules and Lua scripts into an authorized application. The executable is
`fpatch` and the current project version is `1.0.0`.

## Preview

Build the Android artifacts first so the host executable can embed them, then
build `fpatch`:

```powershell
./scripts/build_android.ps1
./scripts/build.ps1
```

Inspect or patch an APK you are authorized to test:

```powershell
fpatch inspect --source app.apk
fpatch inspect --source app.apk --ndk
fpatch inject --source app.apk --lua-entry smoke.lua
fpatch inject --profile example_fp_profile.yaml
```

`inspect` reports manifest, signing, DEX, JNI, and ELF details. `inject` patches
binary Android XML directly, adds the embedded bootstrap/runtime and payload,
aligns the APK, and resigns the base plus supplied splits with one key.

See the [full command tutorial](docs/COMMANDS.md), [build guide](docs/BUILD.md),
and [injection/extension reference](docs/inject.md).
