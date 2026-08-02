# FalconPatch

A command-line Android testing tool that loads developer-provided native `.so` modules or Lua scripts into an authorized application. You can get started with [docs/BUILD.md](docs/BUILD.md)

## Features

### Inspect APKs

```sh
fpatch inspect --source app.apk
fpatch inspect --source app.apk --ndk
```

`inspect` previews APK metadata, signing, DEX/native inventory, Java/Kotlin to
NDK calls, and FalconPatch bootstrap readiness. Use `--ndk` for a focused native
library report.

---

See [documentation](docs/COMMANDS.md) for the full command reference.