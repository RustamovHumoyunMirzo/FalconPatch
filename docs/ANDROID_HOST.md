# Android Host Integration

FalconPatch is intended for apps you own or have permission to test. The host
app opts in by loading modules from an app-private directory in debug or
internal QA builds.

## Expected Flow

1. Build the target app with FalconPatch enabled only for a debug/internal
   flavor.
2. Use `fpatch init` to create a local module workspace.
3. Add `.so` or `.lua` modules with `fpatch add-so` or `fpatch add-lua`.
4. Push the generated manifest and module files into the app-private
   FalconPatch directory with your normal development tooling.
5. The app reads the manifest, loads approved modules, and exposes only the
   API surface declared by the host app.

## Native Module Contract

Native modules should export:

```c
#include "fpatch_api.h"

int fpatch_module_init(const fpatch_host_api *api) {
    api->log(1, "demo", "module loaded");
    return 0;
}

void fpatch_module_shutdown(void) {
}
```

## Lua Module Contract

Lua modules should return a table with an `init` function:

```lua
return {
  init = function(api)
    api.log("demo lua module loaded")
  end
}
```

## Security Defaults

- Do not enable FalconPatch in production builds.
- Keep module files in app-private storage.
- Validate module names, hashes, ABI, and manifest schema before loading.
- Expose a small host API instead of giving modules unrestricted app access.
- Require explicit developer action to install or enable a module.
