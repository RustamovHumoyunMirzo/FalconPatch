# Changelog

## 1.9.0 - 2026-09-05

- Added `fpatch.hookMethod(...)` for synchronous in-process interception of
  static and instance calls made through `Java.use(...)`.
- Added optional JVM-signature matching, ordered hook chaining, enable/disable,
  removal, call/failure statistics, and recursion protection.
- Added `fpatch.hookCapabilities()` so scripts can detect the active
  `reflection-bridge` backend and the absence of unsafe raw `ArtMethod` patching.
- Hardened reflection object access for concurrent calls, object parameters,
  array descriptors, and handle-id rollover.
- Added declarative `dex_patches` to JSON/YAML injection profiles with exact
  class/method descriptor matching and constant boolean, zero, null, or void
  return actions.
- Added target-DEX string replacement with equal MUTF-8/UTF-16 length checks,
  bounds-checked parsing, fail-closed selector matching, and refreshed DEX SHA-1
  signatures and Adler-32 checksums.
- Added injection plan/result counts and focused profile/bytecode transformer
  tests.

## 1.8.0 - 2026-09-02

- Added `fpatch inspect-module` for focused native module inspection by APK,
  target library, and optional ABI filters.
- Added module evidence reporting for Java/Kotlin native loads, JNI exports,
  likely registered JNI links, native callers, and outbound `.so` references.
- Updated `inspect` load-call analysis to include `Runtime.load` and
  `Runtime.loadLibrary`.
- Added a built-in Lua `assets` module for injected asset reads, listing,
  normalized paths, and ImageView/WebView handles.
- Removed the detached `background-worker` Lua module from the built-in Android
  runtime.
- Expanded global Lua helpers with ABI, runtime metadata, wall/monotonic clocks,
  and a protected-call helper.
- Added convenience aliases and an `app.info()` snapshot across built-in Lua
  modules.
- Added global `Java.use(...)` and `jni.use(...)` reflection handles for static
  calls, constructors, private field access, instance calls, and object handle
  release.
- Moved example profile references to `examples/` across docs and tests.
- Updated project, CLI, and artifact documentation version references to
  `1.8.0`.

## 1.7.2 - 2026-08-28

- Completed the 4/4 `detach --smart-repair` pass with `System.load`,
  `System.loadLibrary`, `Runtime.load`, and `Runtime.loadLibrary` bytecode
  repair.
- Added static JNI callsite repair with exact short and long JNI symbol matching,
  including overloaded `Java_...__signature` exports.
- Added registered-JNI repair for removed libraries that expose
  `RegisterNatives` class, method, and JNI signature strings.
- Added safe native return synthesis for object, boolean, 32-bit primitive, and
  wide `long`/`double` callsites while preserving method size.
- Fixed repaired DEX header updates to use the required SHA-1 signature plus
  Adler-32 checksum.
- Hardened smart-repair DEX parsing with overflow-safe table and parameter-list
  bounds checks to reduce malformed-APK crash risk.
- Expanded smart-repair CLI reporting and tests for runtime loads, static JNI,
  registered JNI, and wide native returns.

## 1.5.0 - 2026-08-06

- Added release and manual artifact packages for prebuilt host/runtime/bootstrap bundles.
- Added `inject --artifacts` support with metadata validation, checksums, and trusted package loading.
- Added richer APK inspection for JNI/native method usage and optional advanced NDK-only reports.
- Added vendored Lua 5.4.8 Android builds to avoid release-time dependency download failures.
- Added built-in Lua modules for app state, lifecycle events, intents, UI overlays, WebView, and background work.
- Added stateful Lua UI handles with overlays, styling, draggable elements, custom events, layouts, grids, lists, switches, images, WebView, and OpenGL surfaces.
- Added release upload hardening for GitHub Actions and Linux host-build fixes for vendored C dependencies.
- Split Lua extension docs into focused module references and expanded command/tutorial docs.

## 1.0.0 - 2026-08-03

- Added `fpatch inspect` with binary manifest, signing, DEX/JNI, and ELF analysis.
- Added advanced `inspect --ndk` reporting.
- Added `fpatch inject` with repeatable CLI inputs and JSON/YAML profiles.
- Added direct binary Android XML patching with provider/service bootstraps.
- Added APK alignment, v1/v2/v3 signing verification, and common-key split resigning.
- Added the modular Android Lua/JNI runtime and `FalconPatch.h` extension ABI.
- Added Java and optional Kotlin bootstrap builds for embedded DEX output.
- Added embedded `FPB1` Lua payloads, user assets, native initializer support, and random runtime filenames.
- Added host/Android PowerShell build scripts, examples, tests, and command documentation.
- Added checksum-verified prebuilt artifact packages and `inject --artifacts`.
- Added commit CI plus manual/release packaging for Windows, Linux, and macOS on x86_64 and arm64.
