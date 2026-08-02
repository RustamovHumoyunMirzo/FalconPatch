# FalconPatch

A command-line Android testing tool that loads developer-provided native `.so` modules or Lua scripts into an authorized application.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Features

### Inspect An APK

```sh
fpatch inspect --source app.apk
```

The inspect command reports APK structure, manifest metadata, debug/test flags,
certificate hash, signature schemes, DEX/native code inventory, and FalconPatch
bootstrap strategy hints.
