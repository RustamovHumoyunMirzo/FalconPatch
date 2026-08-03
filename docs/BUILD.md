# Build FalconPatch

FalconPatch has two independent builds. The Android build produces runtime
libraries and bootstrap DEX files. The host build embeds whatever Android
artifacts are present and produces `fpatch`.

## Requirements

- CMake 3.24 or newer and a C compiler for the host CLI
- Android SDK with a platform and build-tools (`d8`, `zipalign`, `apksigner`)
- Android NDK for `libfalconpatch.so`
- JDK for the Java bootstrap and signing
- Ninja for Android builds
- optional `kotlinc` for the Kotlin bootstrap variant

Set `ANDROID_SDK_ROOT` and `ANDROID_NDK_HOME` when the tools are not in their
standard SDK locations.

## Android Runtime

Build all supported ABIs:

```powershell
./scripts/build_android.ps1 -Configuration Release
```

Build selected ABIs:

```powershell
./scripts/build_android.ps1 -Abis arm64-v8a,x86_64 -Api 21
```

The script writes:

```text
dist/android/arm64-v8a/libfalconpatch.so
dist/android/armeabi-v7a/libfalconpatch.so
dist/android/x86/libfalconpatch.so
dist/android/x86_64/libfalconpatch.so
dist/android/bootstrap/java/classes.dex
dist/android/bootstrap/kotlin/classes.dex   # when kotlinc is available
dist/android/sdk/include/FalconPatch.h
dist/android/sdk/include/lua*.h
```

Only compile the Java/Kotlin bootstrap:

```powershell
./scripts/build_android.ps1 -BootstrapOnly
```

The Android CMake project downloads pinned Lua 5.4.8 source and verifies its
SHA-256 checksum. Each ABI has its own object/build directory; later ABIs reuse
only the downloaded source.

## Host CLI

Run the host wrapper after the Android build so those files are embedded:

```powershell
./scripts/build.ps1 -Configuration Release
```

The executable is copied to `dist/fpatch.exe` on Windows or `dist/fpatch` on
Unix-like hosts.

Direct CMake builds are also supported:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

On Windows, the direct debug executable is usually:

```text
build\Debug\fpatch.exe
```

If `inject` says no embedded runtime is available, run the Android build and
then rebuild the host executable. A direct host-only build still provides
`inspect`, profile dry-runs, and global help/version commands.

---

[< Go Back](../README.md) | [Next >](COMMANDS.md)
