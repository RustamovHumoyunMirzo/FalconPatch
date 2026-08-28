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

It is not a complete bytecode repair tool yet. If the app itself still calls
`System.loadLibrary("mylib")`, the app may still fail at runtime after the
library is removed. FalconPatch keeps `--smart-repair` guarded until DEX
call-site rewriting is implemented safely.

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

`--smart-repair` is intentionally blocked right now:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk --smart-repair
```

FalconPatch returns an error explaining that DEX call-site rewriting is not
enabled yet. This is deliberate. Safely removing calls such as:

```java
System.loadLibrary("demo");
```

requires more than deleting bytes. A production repair pass needs to:

- find literal and indirect load calls across every DEX
- understand whether the load result affects later control flow
- replace the load with a safe no-op or guarded failure path
- preserve verifier-valid bytecode, registers, try/catch ranges, and checksums
- keep multidex ordering and APK signing valid

Until that exists, `detach` only removes ZIP-level native payloads.

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
```

If no matching library entry exists, the command fails instead of producing an
unchanged APK.

## Troubleshooting

- `detach requires --target, --so, and --out`: one of the required flags is missing.
- `Output APK must not overwrite the target APK`: choose a different `--out`.
- `No matching libdemo.so entry was found`: check the library name and ABI.
- `--smart-repair needs DEX call-site rewriting`: run without `--smart-repair`.
- `zipalign was not found`: install Android SDK build-tools or omit `--sign`.
- `Java was not found`: install a JDK or omit `--sign`.
- `apksigner.jar was not found`: install Android SDK build-tools or omit `--sign`.

---

[< Inject Command](inject.md) | [Extensions >](extensions/README.md)
