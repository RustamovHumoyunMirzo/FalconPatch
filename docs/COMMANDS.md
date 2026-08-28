# FalconPatch Command Tutorial

FalconPatch is intended for applications you own or are explicitly authorized
to assess. Injection changes APK contents and invalidates the original
signature, so keep the source APK and signing material backed up.

## Build Order

`fpatch` embeds the Android runtimes and bootstrap DEX at host build time. Build
in this order:

```powershell
./scripts/build_android.ps1 -Configuration Release
./scripts/build.ps1 -Configuration Release
```

See [BUILD.md](BUILD.md) for tool discovery, selected ABIs, and direct CMake
commands.

## Prebuilt Artifact Packages

Release and manual artifact runs produce these host packages:

```text
windows-x86_64.tar.gz  windows-arm64.tar.gz
linux-x86_64.tar.gz    linux-arm64.tar.gz
macos-x86_64.tar.gz    macos-arm64.tar.gz
```

Each package contains the matching `fpatch` executable, all four Android
`libfalconpatch.so` ABIs, Java and Kotlin bootstrap DEX files, SDK headers,
`falconpatch-artifacts.json`, and the project license. You can extract the
executable and also pass the original archive directly to injection:

```powershell
tar -xzf downloads/windows-x86_64.tar.gz -C tools/falconpatch
tools/falconpatch/host/fpatch.exe --version
tools/falconpatch/host/fpatch.exe inject --source app.apk --lua-entry smoke.lua \
  --artifacts downloads/windows-x86_64.tar.gz
```

This avoids a local C/C++, NDK, Java compiler, or Kotlin compiler build. APK
signing still needs Android SDK build-tools and a JDK; use `--no-sign` only when
another signing step will follow.

The metadata declares the package platform/architecture and every file's
resource kind, logical name, archive path, byte size, and SHA-256. `fpatch`
selects runtime ABIs and the requested bootstrap from those declarations; it
does not guess from filenames. It rejects unsafe archive paths, duplicate or
undeclared files, unsupported schema/runtime API versions, size mismatches, and
checksum mismatches. A checksum detects damage but does not prove who produced
an archive, so use packages from a trusted release.

The manifest contract starts like this; every packaged file has one entry:

```json
{
  "schema_version": 1,
  "runtime_api": 1,
  "falconpatch_version": "1.5.0",
  "package": "linux-arm64",
  "host": { "platform": "linux", "arch": "arm64" },
  "files": [
    {
      "kind": "runtime",
      "name": "arm64-v8a",
      "path": "android/runtime/arm64-v8a/libfalconpatch.so",
      "size": 123456,
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
    }
  ]
}
```

The all-zero checksum above is illustrative; generated manifests contain the
actual digest.

Validate a package without writing an APK:

```powershell
fpatch inject --source app.apk --artifacts linux-arm64.tar.gz --dry-run
```

## Global Commands

```powershell
fpatch
fpatch --help
fpatch --version
fpatch -v
```

With no arguments, `fpatch` prints the author/copyright notice and a help hint.
`--version` and `-v` print `1.5.0`.

## Inspect An APK

```powershell
fpatch inspect --source app.apk
fpatch inspect --apk app.apk
```

The report covers package/version/SDK metadata, debuggable and test-only flags,
certificate SHA-256, v2/v3 signature blocks, DEX inventory, application class,
native ABIs, ELF libraries, `System.load`/`System.loadLibrary` calls, declared
native methods and their parameter/return types, and bootstrap strategy hints.

Use advanced NDK-only mode for ELF and ZIP details:

```powershell
fpatch inspect --source app.apk --ndk
```

See [inspect.md](inspect.md) for annotated report examples.

## Inject: Smallest Example

Add one startup script, align the result, and sign it with an automatically
generated local debug key:

```powershell
fpatch inject --source app.apk --lua-entry examples/hello.lua
```

The default output is `app-fpatch.apk`. FalconPatch does not overwrite the
source APK. Use `--output` to choose another path:

```powershell
fpatch inject --source app.apk --output build/app-debug.apk \
  --lua-entry examples/hello.lua
```

PowerShell users can put the command on one line or replace `\` with a backtick.

## Native Libraries

`--native` is repeatable. FalconPatch reads the ELF header and infers
`armeabi-v7a`, `arm64-v8a`, `x86`, or `x86_64`:

```powershell
fpatch inject --source app.apk \
  --native out/arm64-v8a/libdiagnostics.so \
  --native out/x86_64/libdiagnostics.so
```

Libraries are placed at `lib/<abi>/<filename>` and stored uncompressed. The
runtime library is loaded first. Each user library is then opened with
`dlopen(..., RTLD_NOW | RTLD_GLOBAL)`.

With no explicit initializer, FalconPatch tries these in order:

1. `falconpatch_init(const FalconPatchHostApi *)`
2. `JNI_OnLoad(JavaVM *, void *)`
3. ELF constructors only

Select a custom initializer by library name:

```powershell
fpatch inject --source app.apk \
  --native out/arm64-v8a/libdiagnostics.so \
  --native-init libdiagnostics.so=diagnostics_start
```

Assign the name exposed to Lua `require(...)`:

```powershell
fpatch inject --source app.apk \
  --native out/arm64-v8a/libexample.so \
  --native-module libexample.so=example
```

Use [FalconPatch.h](../android/include/FalconPatch.h) and the headers copied to
`dist/android/sdk/include` to build extensions. A complete starter is under
[`examples/native-extension`](../examples/native-extension).

## Lua Scripts

Entry scripts execute once when the bootstrap provider initializes:

```powershell
fpatch inject --source app.apk --lua-entry startup.lua
```

Non-entry scripts become embedded `require` modules. Their default module name
is the filename without `.lua`; profiles can set an explicit `module` value:

```lua
local helper = require("helper")
fpatch.log(4, helper.runtime_version())
```

Lua files are not stored directly in APK assets. They are serialized into
`assets/falconpatch/runtime.bin` using the bounded `FPB1` archive format and
loaded from memory.

See the [extension index](extensions/README.md) for the restricted Lua
environment and separate references for globals, app state, JNI, UI overlays,
events, intents, background workers, pure Lua scripts, and native Lua modules.

For example, a startup script can show a diagnostic overlay in the current
activity and drain lifecycle events captured by the bootstrap:

```lua
local app = require("app")
local ui = require("ui")
local events = require("events")

ui.overlay("FalconPatch", app.package_name() or "unknown package")
for _, event in ipairs(events.drain()) do
    fpatch.log(4, event)
end
```

## Detach A Native Library

`detach` removes a named `.so` from an APK and strips old APK signature entries.
The library selector accepts `mylib`, `libmylib`, `mylib.so`, or
`libmylib.so`; all normalize to `libmylib.so`.

```powershell
fpatch detach --target app-fpatch.apk --so mylib --out app-detached.apk
```

By default every ABI is edited. To target only selected ABI folders:

```powershell
fpatch detach --target app-fpatch.apk --so mylib --abi arm64-v8a --out app-detached.apk
fpatch detach --target app-fpatch.apk --so mylib -a --out app-detached.apk
```

Unsigned detach outputs do not require Android SDK signing tools. Add `--sign`
to align, sign, and verify using the same keystore flags as `inject`:

```powershell
fpatch detach --target app-fpatch.apk --so mylib --out app-detached.apk --sign \
  --keystore debug.keystore --ks-alias androiddebugkey \
  --ks-pass android --key-pass android
```

`--smart-repair` patches literal `System.loadLibrary("mylib")`,
`System.load(".../libmylib.so")`, and safe static-JNI callsites backed by
exports in the removed `.so`. It replaces repaired invokes with same-size
no-ops, writes safe default results where possible, and refreshes DEX checksums.

See [detach.md](detach.md) for ABI selection, signing details, FalconPatch
runtime removal behavior, and smart-repair details.

## JSON And YAML Profiles

Both formats use the same schema. Run either example without writing output:

```powershell
fpatch inject --profile example_fp_profile.json --dry-run
fpatch inject --profile example_fp_profile.yaml --dry-run
```

Paths in a profile are resolved relative to the profile file. CLI values
override scalar profile values and append repeatable inputs.

```yaml
name: authorized-smoke-test
source: app.apk
output: build/app-fpatch.apk
artifacts: packages/windows-x86_64.tar.gz
native:
  arm64-v8a:
    - path: modules/arm64-v8a/libexample.so
      name: example
      module: example
      init: falconpatch_init
scripts:
  lua:
    - path: scripts/helper.lua
      module: helper
      entry: false
    - path: scripts/startup.lua
      entry: true
assets:
  - assets/debug-overlay.png
splits:
  - splits/split_config.arm64_v8a.apk
strategy: auto
bootstrap_language: java
random_libname: false
signing:
  keystore: keys/debug.keystore
  alias: androiddebugkey
  store_password: android
  key_password: android
```

The optional `artifacts` path is resolved relative to the profile, just like the
source and payload inputs. A CLI `--artifacts` value overrides it. Native input
fields are `path`, optional `abi`, optional APK `name`, optional
`init`, and optional Lua `module`. Lua fields are `path`, optional `module`, and
`entry`.

## Strategies

`--strategy auto` is the default. It first adds the provider/service bootstrap
without changing app debug policy. If that manifest edit cannot be produced,
it retries the provider bootstrap with `android:debuggable="true"`.

Fixed strategies never fall back:

- `provider`: provider/service bootstrap, original debuggable state preserved
- `provider-debuggable`: same bootstrap plus `debuggable=true`

The non-exported provider initializes during app process startup, starts the
runtime directly, and attempts to start the registered non-exported service.
Service startup policy failures are caught because the provider has already
performed initialization. Runtime failures are logged and do not terminate the
host application.

Choose Java or Kotlin bootstrap classes:

```powershell
fpatch inject --source app.apk --bootstrap-language java
fpatch inject --source app.apk --bootstrap-language kotlin
```

The selected DEX must exist in the supplied artifact package or have been
embedded when `fpatch` was built.

## Runtime Library Name

The default runtime is `libfalconpatch.so`. To use an alphabetic random APK
filename and matching manifest metadata:

```powershell
fpatch inject --source app.apk --random-libname
```

The generated name is reported after injection. User libraries still retain
their configured filenames and module names.

## Assets

`--asset` is repeatable. Files are written under
`assets/falconpatch/user/<basename>`:

```powershell
fpatch inject --source app.apk \
  --asset debug-overlay.png \
  --asset test-data.json
```

Asset basenames must be unique to prevent accidental replacement.

## Split APKs

Pass the base APK as `--source` and every installed split as `--split`:

```powershell
fpatch inject --source base.apk \
  --split split_config.arm64_v8a.apk \
  --split split_config.en.apk \
  --output build/base-fpatch.apk
```

FalconPatch inspects ABIs across the set, patches the base, strips stale
signature entries from every split, aligns every output, and signs all outputs
with the same key. Split outputs use `<original-name>-fpatch.apk` beside the
base output. Extract `.apks` containers first and pass their APK members
explicitly; FalconPatch does not rewrite bundletool `toc.pb` metadata.

## Signing

By default FalconPatch creates `falconpatch-debug.keystore` beside the output
and uses alias/password `androiddebugkey`/`android`. Supply release or assessment
credentials explicitly when required:

```powershell
fpatch inject --source app.apk \
  --keystore keys/test.keystore \
  --ks-alias testkey \
  --ks-pass changeit \
  --key-pass changeit
```

The pipeline is patch, `zipalign -p 4`, `apksigner sign`, then `apksigner
verify`. Existing output APKs are replaced only after temporary outputs pass
structural verification. The original app signature cannot be preserved
without its private key.

For an intentionally unsigned artifact:

```powershell
fpatch inject --source app.apk --no-sign
```

The APK is still aligned but cannot normally be installed until signed.

## Verification And Troubleshooting

Inspect the result:

```powershell
fpatch inspect --source app-fpatch.apk
```

Expected changes include one additional DEX, `Existing bootstrap: found`, the
embedded runtime ABI(s), and a new signing certificate when signing is enabled.

Common failures:

- `No embedded Android runtime`: pass a release package with `--artifacts`, or
  run `build_android.ps1` and rebuild `fpatch`.
- `No embedded ... bootstrap DEX`: pass a release package with `--artifacts`,
  or build that DEX and rebuild `fpatch`.
- `Artifact file checksum does not match`: download the package again from a trusted release.
- `zipalign/apksigner not found`: install Android SDK build-tools or set `ANDROID_SDK_ROOT`.
- `keytool/Java not found`: install a JDK or use `--no-sign` and sign separately.
- `Native ABI mismatch`: fix the profile ABI or supply the correct `.so`.
- `FalconPatch bootstrap is already present`: start from the original APK.

## Complete CLI Example

```powershell
fpatch inject \
  --source base.apk \
  --output build/base-fpatch.apk \
  --artifacts downloads/linux-x86_64.tar.gz \
  --native modules/arm64-v8a/libexample.so \
  --native-module libexample.so=example \
  --lua scripts/helper.lua \
  --lua-entry scripts/startup.lua \
  --asset assets/debug-overlay.png \
  --strategy auto \
  --random-libname
```

---

[< Build Guide](BUILD.md) | [Inspect Command >](inspect.md)
