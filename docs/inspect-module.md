# Inspect-Module Command

Use `inspect-module` to focus the APK report on one native library.

```powershell
fpatch inspect-module --source app.apk --target libmylib
fpatch inspect-module --source app.apk --target mylib --abi arm64-v8a
```

`--target` accepts the same normalized forms as `detach`: `mylib`,
`libmylib`, `mylib.so`, or `libmylib.so`.

## What It Reports

- matching APK entries under `lib/<abi>/`
- ABI, size, compression, and ELF validity from the shared APK inspector
- Java/Kotlin `System.load`, `System.loadLibrary`, `Runtime.load`, and
  `Runtime.loadLibrary` calls that resolve to the selected module
- static JNI exports physically present in the selected `.so`
- likely registered-JNI evidence from `RegisterNatives` string data
- DEX `native` declarations that appear backed by the selected module
- other native modules that mention the selected module name or `.so` filename
- outbound `.so` strings referenced by the selected module

## ABI Selection

By default, all ABIs are inspected:

```powershell
fpatch inspect-module --source app.apk --target demo
```

Limit the report to one or more ABIs:

```powershell
fpatch inspect-module --source app.apk --target demo --abi arm64-v8a
fpatch inspect-module --source app.apk --target demo --abi arm64-v8a --abi x86_64
```

Use `--abi all` to make an explicit all-ABI command.

## Evidence Model

The command reports evidence FalconPatch can physically read from the APK. It
does not emulate app startup, run native code, decrypt packed strings, or guess
runtime-only loader behavior. Native caller detection is string-based, so it can
identify common `dlopen("libdemo.so")` style links but not pointer-built or
encrypted names.

For repair-oriented cleanup, pair this command with `detach --smart-repair`.

---

[< Inspect Command](inspect.md) | [Detach Command >](detach.md)
