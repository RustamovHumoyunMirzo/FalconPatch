# Detach Command

Use `detach` to remove a native library from an APK you own or are explicitly
authorized to modify. It rewrites the APK ZIP, removes stale APK v1 signature
entries under `META-INF`, and optionally signs the output with the same signing
toolchain used by `inject`.

```powershell
fpatch detach --target app-fpatch.apk --so mylib --out app-detached.apk
```

## When To Use It

`detach` is useful when you want to:

- remove a test module from a patched APK without rebuilding the original app
- remove a FalconPatch-loaded helper library from one or more ABI folders
- produce an unsigned intermediate APK for a later signing pipeline
- produce a signed APK after removing a library

For apps that call the library directly, add `--smart-repair` so FalconPatch
patches Java/Kotlin load sites and safe JNI call sites while rebuilding the APK.

## Required Flags

| Flag | Description |
| --- | --- |
| `--target <file.apk>` | APK to edit. FalconPatch never edits this file in place. |
| `--so <name>` | Native library to remove. Prefix and suffix are optional. |
| `--out <file.apk>` | Output APK path. Must be different from `--target`. |

## Library Name Matching

The `--so` selector normalizes common forms to the APK filename:

| Input | Normalized APK entry filename |
| --- | --- |
| `demo` | `libdemo.so` |
| `libdemo` | `libdemo.so` |
| `demo.so` | `libdemo.so` |
| `libdemo.so` | `libdemo.so` |

Only entries named `lib/<abi>/<normalized-name>` are removed. Other files with
the same text in their path are left alone.

## ABI Selection

By default, `detach` targets every ABI folder that contains the selected
library:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk
```

Target a single ABI:

```powershell
fpatch detach --target app-fpatch.apk --so demo --abi arm64-v8a --out app-detached.apk
```

Target multiple ABIs by repeating `--abi`:

```powershell
fpatch detach --target app-fpatch.apk --so demo \
  --abi arm64-v8a \
  --abi x86_64 \
  --out app-detached.apk
```

Use `-a` or `--abi all` to make an explicit "all ABIs" command:

```powershell
fpatch detach --target app-fpatch.apk --so demo -a --out app-detached.apk
fpatch detach --target app-fpatch.apk --so demo --abi all --out app-detached.apk
```

## Signing

By default, `detach` writes an unsigned APK and does not require Android SDK
build-tools:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk
```

Because ZIP contents changed and old signature entries are removed, this APK is
normally not installable until another signing step signs it.

Add `--sign` to align, sign, and verify the output:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk --sign
```

When `--sign` is present, FalconPatch uses the same defaults as `inject`:

| Flag | Default |
| --- | --- |
| `--keystore <file>` | `falconpatch-debug.keystore` beside the output APK |
| `--ks-alias <name>` | `androiddebugkey` |
| `--ks-pass <password>` | `android` |
| `--key-pass <password>` | `android` |

Custom signing example:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk --sign \
  --keystore debug.keystore \
  --ks-alias androiddebugkey \
  --ks-pass android \
  --key-pass android
```

Signing requires:

- Android SDK build-tools with `zipalign` and `apksigner.jar`
- JDK tools including `java`
- `keytool` when FalconPatch needs to generate the default debug keystore

## FalconPatch Runtime Detach

You can remove the FalconPatch runtime library itself:

```powershell
fpatch detach --target app-fpatch.apk --so falconpatch --out app-no-runtime.apk
```

When the normalized library is `libfalconpatch.so`, `detach` also removes
`assets/falconpatch/runtime.bin`, because that payload is only useful to the
FalconPatch runtime.

Current limitation: the bootstrap provider/service DEX and manifest entries are
not removed yet. That needs DEX and binary Android XML removal logic, not just
ZIP entry filtering. Without `libfalconpatch.so`, the bootstrap should fail
closed and let the host app continue, but a future smart repair pass should
remove those bootstrap references too.

## Smart Repair

`--smart-repair` scans every `classes*.dex` entry and the removed `.so` files.
It repairs literal native-load calls that point at the detached library:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk --smart-repair
```

It handles the common direct load forms:

```java
System.loadLibrary("demo");
System.load("/data/local/tmp/libdemo.so");
Runtime.getRuntime().loadLibrary("demo");
Runtime.getRuntime().load("/data/local/tmp/libdemo.so");
```

FalconPatch also scans the removed native libraries for static JNI exports such
as:

```text
Java_com_example_Bridge_ping
Java_com_example_Bridge_ping__Ljava_lang_String_2
```

It also recognizes many dynamic `RegisterNatives` libraries by matching native
string evidence against the DEX declaration:

- the owning class name, such as `com/example/Bridge`
- the native method name, such as `ping`
- the JNI prototype string, such as `()I` or `(Ljava/lang/String;)V`

When a DEX declares a matching `native` method and Java/Kotlin code directly
invokes that method, smart repair removes the invoke too. Void calls are
no-oped. Object, boolean, and 32-bit primitive returns are replaced with
default `null`, `false`, or `0` values. `long` and `double` calls followed by
`move-result-wide` are rewritten to `const-wide/16 0` plus padding.

The repair is intentionally size-preserving. FalconPatch replaces only the
matching invoke instruction with the same number of Dalvik `nop` code units.
It does not delete instructions, change branch targets, resize methods, or move
try/catch regions. After patching a DEX file it recomputes the DEX SHA-1
signature and Adler-32 checksum.

This makes the output verifier-friendly for direct literal load calls, static
JNI exports, and most registered-JNI callsites that leave evidence in the
native binary. Cases that still require app-specific analysis are reported as
zero repaired or skipped calls and left unchanged:

- library names built from variables or encrypted strings
- reflection calls that eventually call `System.loadLibrary`
- custom native loaders
- `RegisterNatives` tables with encrypted/packed names or signatures
- framework wrappers that hide the actual load or JNI call target
- callsites that need branch, try/catch, or method-size rewriting

Run `inspect` before and after detach to see literal load calls that FalconPatch
can identify:

```powershell
fpatch inspect --source app-fpatch.apk
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk --smart-repair
fpatch inspect --source app-detached.apk
```

## Output Summary

On success, the command prints the normalized library, signing mode, output APK,
and removed ZIP entries:

```text
FalconPatch detach complete
  Library: libdemo.so
  Signing: unsigned
  Output: app-detached.apk
  Removed:
    lib/arm64-v8a/libdemo.so
  Smart repair load calls: 1
  Smart repair JNI exports: 4
  Smart repair RegisterNatives: detected
  Smart repair native calls: 2
```

If no matching library entry exists, the command fails instead of producing an
unchanged APK.

## Troubleshooting

- `detach requires --target, --so, and --out`: one of the required flags is missing.
- `Output APK must not overwrite the target APK`: choose a different `--out`.
- `No matching libdemo.so entry was found`: check the library name and ABI.
- `Smart repair load calls: 0`: no literal `System.load`, `System.loadLibrary`, `Runtime.load`, or `Runtime.loadLibrary` call matched the detached library.
- `Smart repair JNI exports: 0`: the removed `.so` did not expose static JNI symbols. Registered JNI may still be detected separately.
- `Smart repair skipped native calls`: FalconPatch found JNI callsites that need a wider rewrite than this size-preserving pass can safely perform.
- `zipalign was not found`: install Android SDK build-tools or omit `--sign`.
- `Java was not found`: install a JDK or omit `--sign`.
- `apksigner.jar was not found`: install Android SDK build-tools or omit `--sign`.

---

[< Inject Command](inject.md) | [Extensions >](extensions/README.md)
