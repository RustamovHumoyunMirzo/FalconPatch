# Injection And Native Extension Reference

The full workflow and all CLI flags are documented in
[COMMANDS.md](COMMANDS.md). This page focuses on the native extension boundary.

## Extension Entry Points

Include `FalconPatch.h` from `dist/android/sdk/include`. A library may expose
either or both optional symbols:

```c
FPATCH_EXPORT int falconpatch_init(const FalconPatchHostApi *host);
FPATCH_EXPORT const FalconPatchLuaModule *falconpatch_lua_module(void);
```

A custom symbol selected with `--native-init` uses the same initializer
signature. Return `0` for success and nonzero to disable FalconPatch startup.

`FalconPatchHostApi` version 1 provides:

- `java_vm`: process `JavaVM *`
- `application_context`: global-reference Android application context
- `get_jni_env()`: obtains/attaches a `JNIEnv *` for the current thread
- `log(priority, tag, message)`: Android log writer
- `register_lua_module(name, open_function)`: add another `require` module

Do not delete `application_context`. It is owned by the runtime.

## Payload Order

At process startup FalconPatch performs this sequence:

1. Java/Kotlin bootstrap loads the runtime library named in manifest metadata.
2. Native runtime reads and validates `assets/falconpatch/runtime.bin`.
3. Restricted Lua state is created.
4. Built-in modules are registered in `package.preload`.
5. User native libraries are loaded and initialized in profile order.
6. User Lua extension descriptors and embedded Lua modules are registered.
7. Lua scripts marked `entry: true` execute in profile order.

Any initialization error is logged. The provider returns without terminating
the application process.

Lua scripts, C extension descriptors, built-in modules, and the restricted
runtime are documented separately in the [extension index](extensions/README.md).

Static profile-driven bytecode changes are documented separately in the
[DEX patch reference](dex-patches.md).

---

[< Inspect Command](inspect.md) | [Detach Command >](detach.md)
