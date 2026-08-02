# Build

Run the following commands to build the debug project:
```sh
cmake -S . -B build
cmake --build build
```

To build the release project, run:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, the debug executable is usually:

```sh
build\Debug\fpatch.exe
```

---

[< Go Back](../README.md) | [Next >](COMMANDS.md)