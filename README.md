# FalconPatch

FalconPatch is a starter project for a developer-owned Android module workflow.
It gives teams a small CLI named `fpatch` for creating a module manifest and
registering native `.so` or Lua modules that a debug/internal Android host app
can load through an explicit API contract.

This project is for apps you own or are authorized to test. Do not use it to
modify third-party apps or bypass platform, app, store, or user protections.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The executable is named `fpatch`.

## Example CLI

```sh
fpatch init demo-workspace
cd demo-workspace
fpatch add-so hello ../examples/native/libhello_module.so arm64-v8a
fpatch add-lua script ../examples/lua/hello.lua
fpatch list
fpatch validate
```

You can also print the example from the CLI:

```sh
fpatch example
```

## Commands

- `fpatch init [project-dir]` creates `fpatch.json` and module directories.
- `fpatch add-so <name> <path-to-so> [abi]` registers a native module.
- `fpatch add-lua <name> <path-to-lua>` registers a Lua module.
- `fpatch list` prints registered modules.
- `fpatch validate` checks manifest entries and file paths.

## Android Host Contract

See [docs/ANDROID_HOST.md](docs/ANDROID_HOST.md) for the debug-only host app
contract and module API.
