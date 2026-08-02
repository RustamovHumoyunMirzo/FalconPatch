Implement `inspect` cmd that inspects android apps, libs we might use:

| Inspect area                 | Library                                        | Recommendation                          |
| ---------------------------- | ---------------------------------------------- | --------------------------------------- |
| APK/ZIP structure            | **libzip**                                     | Best primary choice                     |
| Compression                  | **zlib**                                       | Usually pulled in by libzip             |
| Binary `AndroidManifest.xml` | Custom parser                                  | Implement from AOSP format              |
| `resources.arsc`             | Custom parser                                  | Implement only fields you need          |
| DEX headers/classes/strings  | Custom lightweight parser                      | Better than embedding ART               |
| Native `.so` files           | **libelf** or **ELFIO via C++**                | Use libelf for pure C                   |
| X.509 certificates           | **OpenSSL libcrypto**                          | Best desktop CLI choice                 |
| SHA-256/checksums            | OpenSSL or mbedTLS                             | Reuse whichever crypto stack you choose |
| JSON output                  | **yyjson**                                     | Fast, compact C library                 |
| Regex/string scanning        | PCRE2, optional                                | Useful for certificate-check heuristics |
| Hash maps/containers         | uthash, optional                               | Header-only C helpers                   |

Output might like this:
```
Source
  File: app.apk
  Type: standalone-apk
  Package: com.example.app
  Version: 2.4.1 (104)
  Min SDK: 26
  Target SDK: 35

Security
  Debuggable: no
  Test-only: no
  Certificate: SHA-256 83:A1:...
  APK Signature Schemes: v2, v3

Code
  DEX files: 3
  Application class: com.example.MainApplication
  Native ABIs: arm64-v8a, armeabi-v7a
  Native libraries: 8

FalconPatch
  Existing bootstrap: not found
  Provider bootstrap possible: yes
  Additional DEX possible: yes
  Manifest patch required: yes
  Resigning required: yes

Strategies
  Integrated loader: unavailable
  JVMTI: unavailable — application is not debuggable
  Startup wrapper: unavailable — application is not debuggable
  Manifest debug patch: available
  Bootstrap APK patch: available```

`fpatch inspect ...flags and stuff`