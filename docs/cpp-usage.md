# Using Jovian from C++

Jovian's native layer is four CMake targets, defined in [`native/CMakeLists.txt`](../native/CMakeLists.txt):

- **`adrastea`** — a static library: the language-neutral Jupyter kernel framework (transport, messaging, the kernel request loop, the abstract `Interpreter` interface). Public headers under [`native/include/adrastea/`](../native/include/adrastea).
- **`elara`** — an executable: embeds R on top of `adrastea`. Public headers under [`native/include/elara/`](../native/include/elara).
- **`carpo`** — an executable: embeds Python (CPython) on top of `adrastea` the same way. Public headers under [`native/include/carpo/`](../native/include/carpo).
- **`themisto`** — an executable: the supervisor that spawns/monitors `elara`/`carpo` processes, one per session, keyed by that session's `kernelType`.

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

This is exactly the shape both Elara and Carpo have (`native/src/carpo/`, built by default via `JOVIAN_BUILD_CARPO`) -- two concrete, working examples proving the extension point genuinely generalizes, not just in theory. `PyInterpreter` (`native/src/carpo/interpreter_py.cpp`) implements the full `Interpreter` interface for real: `executeRequestImpl`/`isCompleteRequestImpl`/`completeRequestImpl`/`inspectRequestImpl` all embed and drive a real CPython interpreter, the same way `RInterpreter` drives R. Its own C API is loaded dynamically at runtime (`native/src/carpo/py/py_dynlib.hpp`, mirroring `native/src/elara/r/r_dynlib.hpp`) rather than linked at build time, for the same version-switching/clean-failure reasons Elara does it for R. A third language kernel would follow the exact same three steps above; nothing about the surrounding scaffold (registration, the executable, the CMake target, its own test suite) needs to change shape to add one.

### Threading contract for interpreter authors

The kernel executes code and reads its sockets on **one thread** (the process's main thread), and every `Interpreter` virtual is called from it — with **one exception**:

- **`interruptRequestImpl()` is called from a different thread**, the *control watcher*, while `executeRequestImpl()` is still running (`KernelCore::executeRequest` brackets the interpreter call with `Server::beginExecution()` / `endExecution()`; between them a watcher thread services `interrupt_request` on the control channel — see [Architecture](architecture/overview.md#inside-a-kernel-process-elara--carpo)). It must therefore only do thread-safe things: flag the runtime to break out of what it is doing, and return `createInterruptReply()`. Do not touch the interpreter's state or call its API from it.
- Only flag an interrupt **while an execution is actually running** (keep an `std::atomic<bool>` set by `executeRequestImpl`, as `RInterpreter` and `PyInterpreter` do). An interrupt with nothing to interrupt would otherwise linger and abort the *next* execution.
- Make the runtime check that flag *and* wake blocking calls. R: set `R_interrupts_pending` (POSIX) / `UserBreak` (Windows); R polls it. Python: a real SIGINT to the interpreter thread (`raise(SIGINT)` on Windows, `pthread_kill` on POSIX) with `signal.default_int_handler` installed, because `PyErr_SetInterrupt()` alone does not wake `time.sleep()`.
- Publishing from the watcher is safe (`ServerZmqImpl` serialises iopub publishing and control replies); everything else, including anything reached through `getInterpreter()`, is not.
- All other control messages (`shutdown_request`, …) sent during an execution are queued and delivered on the main thread afterwards.

Also part of the contract: `executeRequestImpl` must call its reply callback exactly once, honour `ExecuteRequestConfig::allow_stdin` for blocking reads (`adrastea::blockingInputRequest`, which throws when stdin is not allowed), evaluate `user_expressions` after a successful execution and return them in `createSuccessfulReply(payload, user_expressions)`, and report `restart` back in `shutdownRequestImpl`'s `createShutdownReply(restart)`.

## Building and testing just the C++ side

```sh
cmake -S . -B dist/native -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build dist/native --config Release

# Tests (a separate build tree, so JOVIAN_BUILD_TESTS=ON doesn't stick around
# in dist/native's cache for future plain builds):
cmake -S . -B dist/native-test -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DJOVIAN_BUILD_TESTS=ON
cmake --build dist/native-test --config Release
ctest --test-dir dist/native-test -C Release --output-on-failure --timeout 180
```

See [Development](development.md) for what each CTest entry covers and the platform prerequisites ([README](../README.md#requirements)).

Requires an R installation (only its headers, at build time — `elara` loads R's shared library dynamically at *runtime*, see [`native/src/elara/r/r_dynlib.hpp`](../native/src/elara/r/r_dynlib.hpp)) and [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set. No Python installation is needed to *build* `carpo` at all -- unlike R, Carpo doesn't even include Python's headers at compile time (see `py_dynlib.hpp`'s file comment for why); a Python install is only needed at runtime, and only to actually run a Python session (`CarpoTest` also needs one, to embed and exercise for real -- it skips itself via `GTEST_SKIP` if `native/test/CMakeLists.txt`'s `find_package(Python3)` doesn't find one at configure time).
