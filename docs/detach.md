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

For apps that call the library directly with literal
`System.loadLibrary("mylib")` or `System.load(".../libmylib.so")` instructions,
add `--smart-repair` so FalconPatch patches those call sites while rebuilding
the APK.

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

`--smart-repair` scans every `classes*.dex` entry and repairs literal
`java.lang.System` native-load calls that point at the detached library:

```powershell
fpatch detach --target app-fpatch.apk --so demo --out app-detached.apk --smart-repair
```

It handles both common forms:

```java
System.loadLibrary("demo");
System.load("/data/local/tmp/libdemo.so");
```

The repair is intentionally size-preserving. FalconPatch replaces only the
matching `invoke-static` or `invoke-static/range` instruction with the same
number of Dalvik `nop` code units. It does not delete instructions, change
branch targets, resize methods, or move try/catch regions. After patching a DEX
file it recomputes the DEX SHA-1 signature and Adler-32 checksum.

This makes the output verifier-friendly for direct literal load calls. Dynamic
or indirect cases are reported as zero repaired calls and left unchanged,
including:

- library names built from variables or encrypted strings
- reflection calls that eventually call `System.loadLibrary`
- custom native loaders
- `Runtime.load` or framework wrappers that are not `java.lang.System`

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
```

If no matching library entry exists, the command fails instead of producing an
unchanged APK.

## Troubleshooting

- `detach requires --target, --so, and --out`: one of the required flags is missing.
- `Output APK must not overwrite the target APK`: choose a different `--out`.
- `No matching libdemo.so entry was found`: check the library name and ABI.
- `Smart repair load calls: 0`: no literal `System.load` or `System.loadLibrary` call matched the detached library.
- `zipalign was not found`: install Android SDK build-tools or omit `--sign`.
- `Java was not found`: install a JDK or omit `--sign`.
- `apksigner.jar was not found`: install Android SDK build-tools or omit `--sign`.

---

[< Inject Command](inject.md) | [Extensions >](extensions/README.md)
