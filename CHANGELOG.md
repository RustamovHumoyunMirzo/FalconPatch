# Changelog

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
