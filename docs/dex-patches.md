# Declarative DEX Patches

FalconPatch 1.9.0 can apply bounded, static DEX transformations while running
`fpatch inject`. Patches live in a JSON or YAML profile and run against every
original `classes*.dex` before the FalconPatch bootstrap DEX is added.

Use this only on applications you are authorized to modify and test.

## Constant Method Returns

Select a class by dotted name and a method by its exact name plus JVM descriptor:

```yaml
dex_patches:
  - target: com.example.app.Config
    method: isDebuggable()Z
    action: return_true
  - target: com.example.app.Account
    method: cachedUser()Lcom/example/app/User;
    action: return_null
  - target: com.example.app.Metrics
    method: sampleCount()J
    action: return_zero
```

| Action | Compatible return type |
| --- | --- |
| `return_true` | boolean (`Z`) |
| `return_false` | boolean (`Z`) |
| `return_zero` | numeric primitive (`B`, `C`, `S`, `I`, `F`, `J`, or `D`) |
| `return_null` | object or array (`L...;` or `[...`) |
| `return_void` | void (`V`) |

The transformer replaces the existing instruction stream without resizing the
method. Abstract/native methods, constructors, and methods containing try/catch
blocks are rejected. Return actions also fail when the selected method has no
implementation, has an incompatible return type, lacks enough registers, or is
too small for the required instructions.

## String Replacement

```yaml
dex_patches:
  - target: com.example.app.Network
    replace_string:
      from: https://api.prod.com
      to: http://10.0.2.2:8080
```

The target class selects the DEX file containing the string table to modify.
DEX strings are interned, so every reference to the same string within that DEX
observes the replacement, including references from other classes.

`from` and `to` must have identical encoded MUTF-8 byte lengths and identical
UTF-16 code-unit lengths. FalconPatch intentionally does not shift string data
or rewrite all dependent offsets. This makes replacement predictable and keeps
the original DEX layout intact.

## Validation

FalconPatch validates table ranges, ULEB128 values, class data, method IDs,
prototypes, code item bounds, and UTF-8 input before writing changes. It then
refreshes the DEX SHA-1 signature and Adler-32 checksum.

Each profile patch must match at least once across the base APK's DEX files.
Injection stops and discards its temporary APK when a target method or string is
missing. The dry run reports the number of configured patches but cannot prove
selectors until it reads and transforms the APK.

JSON profiles use the same structure:

```json
{
  "dex_patches": [
    {
      "target": "com.example.app.Config",
      "method": "isDebuggable()Z",
      "action": "return_true"
    }
  ]
}
```

---

[< Command tutorial](COMMANDS.md) | [Injection reference >](inject.md)
