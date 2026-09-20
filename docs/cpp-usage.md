# Using Jovian from C++

Jovian's native layer is three CMake targets, defined in [`native/CMakeLists.txt`](../native/CMakeLists.txt):

- **`adrastea`** — a static library: the language-neutral Jupyter kernel framework (transport, messaging, the kernel request loop, the abstract `Interpreter` interface). Public headers under [`native/include/adrastea/`](../native/include/adrastea).
- **`elara`** — an executable: embeds R on top of `adrastea`. Public headers under [`native/include/elara/`](../native/include/elara).
- **`themisto`** — an executable: the supervisor that spawns/monitors `elara` processes.

## Current state: in-tree only

There is no installed/exported CMake package today — no `install()`, no `<Package>Config.cmake`, no pkg-config file. `adrastea` is only ever consumed via `add_subdirectory` from within this same repository (see how `elara`/`themisto` link it in `native/CMakeLists.txt`). If you want to link against `adrastea` from a separate CMake project right now, the only supported path is:

```cmake
add_subdirectory(path/to/jovian/native adrastea-build)
target_link_libraries(your_target PRIVATE adrastea)
```

This pulls in `adrastea`'s `PUBLIC` include directories, compile definitions (`ADRASTEA_STATIC_LIB`), and link libraries (nlohmann_json, cppzmq, OpenSSL::Crypto) automatically, the same way `elara`/`themisto` get them.

Building a real `find_package(adrastea)`-style exported package (an `install(TARGETS ... EXPORT ...)` + generated config/version files) is a reasonable follow-up if an external consumer actually needs one — it hasn't been built yet because nothing outside this repo currently needs it.

## Writing a new interpreter (a new language kernel)

`adrastea::Interpreter` ([`native/include/adrastea/interpreter.hpp`](../native/include/adrastea/interpreter.hpp)) is the extension point. A new language kernel:

1. Subclasses `Interpreter` and implements its `*Impl()` virtual methods (`configureImpl`, `executeRequestImpl`, `completeRequestImpl`, `kernelInfoRequestImpl`, etc.) — see [`native/src/elara/r/interpreter_r.cpp`](../native/src/elara/r/interpreter_r.cpp)'s `RInterpreter` for a complete example.
2. Calls `adrastea::registerInterpreter(this)` once constructed, before anything calls `adrastea::getInterpreter()`.
3. Gets embedded into its own executable the way `elara.cpp` does — `main()` parses CLI args, builds an `adrastea::KernelConfiguration`, constructs the interpreter, and runs `adrastea::Kernel::start()` (see [`native/src/elara/bridge/engine.cpp`](../native/src/elara/bridge/engine.cpp)'s `Server::start()`).

This is exactly the shape Elara has -- and exactly what Carpo (`native/src/carpo/`, opt-in via `-DJOVIAN_BUILD_CARPO=ON`) already does today, as a concrete, buildable, testable example: `PyInterpreter` (`native/src/carpo/interpreter_py.cpp`) implements the full `Interpreter` interface, `kernelInfoRequestImpl()` for real, and every other `*RequestImpl()` as a clear "not implemented" stub rather than a real Python embedding. Turning Carpo into an actually-working Python kernel means replacing those stubs with real CPython embedding (most likely dynamically loading `libpython` at runtime, mirroring `native/src/elara/r/r_dynlib.hpp`, for the same version-switching/clean-failure reasons) -- the surrounding scaffold (registration, the executable, the CMake target, its own test suite) doesn't need to change shape to do that.

## Building and testing just the C++ side

```sh
cmake -S . -B dist/native -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build dist/native --config Release

# Tests (a separate build tree, so JOVIAN_BUILD_TESTS=ON doesn't stick around
# in dist/native's cache for future plain builds):
cmake -S . -B dist/native-test -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DJOVIAN_BUILD_TESTS=ON
cmake --build dist/native-test --config Release
ctest --test-dir dist/native-test -C Release --output-on-failure
```

Requires an R installation (only its headers, at build time — `elara` loads R's shared library dynamically at *runtime*, see [`native/src/elara/r/r_dynlib.hpp`](../native/src/elara/r/r_dynlib.hpp)) and [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set.
