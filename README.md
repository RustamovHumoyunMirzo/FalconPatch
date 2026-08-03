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

`inspect` reports manifest, signing, DEX, JNI, and ELF details.

```powershell
fpatch inspect --source app.apk
fpatch inspect --source app.apk --ndk
```
### Inject into APKs

`inject` patches binary Android XML directly, adds the embedded bootstrap/runtime and payload, aligns the APK, and resigns the base plus supplied splits with one key. Official release packages provide `fpatch`, every Android runtime ABI, both bootstrap DEX variants, and SDK headers, so using `--artifacts` does not require building the NDK, Java, or Kotlin projects locally.

```powershell
fpatch inject --source app.apk --lua-entry smoke.lua
fpatch inject --profile example_fp_profile.yaml
fpatch inject --source app.apk --lua-entry smoke.lua --artifacts windows-x86_64.tar.gz
```

## Documentation

You can get started with [build guide](docs/BUILD.md) or see the [full command tutorial](docs/COMMANDS.md), [native extension reference](docs/inject.md), and [extension references](docs/extensions/README.md).