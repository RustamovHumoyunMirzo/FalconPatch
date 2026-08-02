# Inspect An APK

Use `inspect` to read APK structure, manifest metadata, signing information,
DEX inventory, native libraries, and FalconPatch strategy hints.

```sh
fpatch inspect --source app.apk
```

`--apk` is supported as an alias:

```sh
fpatch inspect --apk app.apk
```

Expected sections:

- `Source`: file type, package name, version, min SDK, target SDK.
- `Security`: debuggable/test-only flags, certificate SHA-256, APK signature schemes.
- `Code`: DEX count, application class, native ABIs, native library count.
- `Java/Kotlin -> NDK`: `System.load`, `System.loadLibrary`, and declared `native` methods.
- `FalconPatch`: whether a bootstrap exists and whether patching/resigning is needed.
- `Strategies`: high-level availability for loader and patch strategies.

Example:

```sh
fpatch inspect --source C:\path\to\app.apk
```

## Inspect Native Code Only

Add `--ndk` to switch `inspect` into advanced native-only mode.

```sh
fpatch inspect --source app.apk --ndk
```

This report focuses on:

- native library count
- ABI list
- 32-bit, 64-bit, mixed, or no native ABI model
- compressed `.so` count
- ELF parse failures
- ABI/header mismatches
- per-library path, ABI, size, ZIP compression, ELF bitness, and ELF machine

Example output shape:

```text
NDK
  File: app.apk
  Package: com.example.app
  Native libraries: 2
  Native ABIs: arm64-v8a
  ABI model: 64-bit only
  Compressed native libraries: 2
  ELF parse failures: 0
  ABI/header mismatches: 0
  Extraction required before load: yes

Native Libraries
  lib/arm64-v8a/libexample.so
    ABI: arm64-v8a
    Size: 123456 bytes
    Compressed size: 78910 bytes
    ZIP compression: deflated
    ELF: 64-bit arm64-v8a
```

## Java/Kotlin To NDK Details

The normal `inspect` report includes a bridge summary under `Code`.

It can show:

- `System.loadLibrary("module")`
- `System.load("/absolute/path/libmodule.so")`
- statically declared `native` methods
- native method parameter types
- native method return type
- the DEX file where each item was found

Example output shape:

```text
Java/Kotlin -> NDK
  Native declarations: 1
  Library load calls: 1
  Modules loaded:
    loadLibrary(example) in com.example.NativeBridge.<clinit> [classes.dex]
  Native methods:
    com.example.NativeBridge.nativeInit(java.lang.String, int): boolean [classes.dex]
```

Dynamic library names can appear as `dynamic/unknown` when the argument is built
at runtime instead of being a literal string in DEX bytecode.

---

[< Go Back](COMMANDS.md)