# Development

## Repository layout

```
CMakeLists.txt, native/CMakeLists.txt, native/test/CMakeLists.txt   the build (see below)
cmake/                  FindR.cmake, FindLibUUID.cmake
vcpkg.json              native dependencies (manifest, pinned baseline)
native/                 C++: adrastea (static lib), elara, carpo, themisto — see architecture/overview.md
lib/                    the TypeScript package
packages/hera/          the R companion package
test/unit/lib/          TypeScript unit tests (Session against a fake WebSocket, ExecutionQueue, …)
test/integration/       end-to-end tests: real themisto + real kernels
tools/playground/       Next.js playground (own package.json)
tools/jupyter-kernelspec/  kernel.json generator
examples/               runnable examples
scripts/                build.js, test.js, clean.js, dev.js, format.js, coverage.js
docs/                   documentation
dist/                   ALL build output (gitignored): dist/lib, dist/native, dist/native-test, dist/ide/*
```

## Build system

**CMake ≥ 3.24, C++23**, dependencies from vcpkg (`vcpkg.json`; the toolchain file is `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`). The root `CMakeLists.txt` resolves `nlohmann_json`, `cppzmq`, `zeromq`, `OpenSSL`, `R` (**headers only** — `cmake/FindR.cmake` runs `R RHOME`), `httplib` and `ixwebsocket`, then adds `native/`.

| Option | Default | Effect |
|---|---|---|
| `JOVIAN_BUILD_ELARA` | ON | Build the `elara` executable |
| `JOVIAN_BUILD_THEMISTO` | ON | Build the `themisto` executable |
| `JOVIAN_BUILD_CARPO` | ON | Build the `carpo` executable |
| `JOVIAN_BUILD_TESTS` | OFF | Build the GoogleTest executables and register them with CTest |
| `JOVIAN_SANITIZE_ADDRESS` | OFF | Address sanitizer |

Outputs go to `dist/native/<config>/` (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`) — executables, and on Windows the vcpkg DLLs next to them. Tests use a **separate build tree** (`dist/native-test`), so `JOVIAN_BUILD_TESTS=ON` does not stick in the main cache — but the output directory is set from the source root, so the test executables land in `dist/native/<config>/` too (which is why `package.json`'s `files` excludes `*_test.exe`).

```sh
# what `npm run build` does (native part):
cmake -S . -B dist/native -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build dist/native --config Release
# TypeScript:
npx tsc --build                # lib/ -> dist/lib
```

Platform notes baked into the build: Windows links the MSVC dynamic runtime, gives `elara` a 128 MiB stack (`/STACK:134217728`, added after a real `STATUS_STACK_OVERFLOW`) and links `bcrypt` for Themisto; Linux links `libuuid` (`find_package(LibUUID)`) and `dl`; macOS links CoreFoundation.

`CMakePresets.json` has Ninja + `cl.exe` presets for Windows IDE use (output under `dist/ide/<preset>`); `npm` scripts never use them.

## Test layers

| Layer | Command | What it needs | What it covers |
|---|---|---|---|
| **Native (GoogleTest / CTest)** | `npm test` (first stage), or by hand: configure `dist/native-test` with `-DJOVIAN_BUILD_TESTS=ON`, build, `ctest --test-dir dist/native-test -C Release --output-on-failure --timeout 180` | vcpkg deps; R + `hera` for `SessionRegistryTest`; Python for `CarpoTest` | 12 CTest entries, below |
| **TypeScript unit** | `npm run test:unit` | built `dist/lib` | `Session` against a fake WebSocket, `ExecutionQueue`, router, middleware, option bodies. No processes. |
| **Integration** | `npm run test:integration` | built native binaries + `dist/lib`, R + `hera`; Python + `carpo` for the Python tests | Real `SessionManager` → `themisto` → `elara`/`carpo`: execute, streaming, stdin (R and Python), history, complete/inspect/is_complete/kernel_info, user expressions, `stopOnError`, interrupt (R and Python), working directory, stderr, comms (client- and kernel-initiated), `shutdown_reply` on stop/restart. Skips itself if `themisto` is not built; the Python tests skip without Python or `carpo`. |

CTest entries: `MessageTest`, `MiddlewareTest`, `AuthenticationTest`, `ZmqSerializerTest`, `KernelConfigurationTest`, `ClientZmqTest`, `ClientHeartbeatTest`, `ClientHandshakeZmqTest` (transport, no R); `ElaraTest` (spawns `elara` for its start-up failure paths); `KernelProcessTest` (spawn/liveness/kill against a dummy helper); `SessionRegistryTest` (drives a real `elara` through `SessionRegistry` — create/execute/restart/stop/interrupt/requests/comms/stop-on-error/user-expressions and concurrency races; ~20 kernel starts, ~25 s locally, 180 s CTest timeout); `CarpoTest` (embeds real Python: execution, streaming, complete/inspect, venv activation).

Playground tests are separate: `npm --prefix tools/playground test`.

The `hera` R package is installed into your R library, not run from the repo: after changing `packages/hera/R/*`, run `npm run hera:install` (CI installs it from source on every run). Features that live in `hera` — such as live streaming of one long-running expression — only work with the updated copy installed.

`npm test` (`scripts/test.js`) runs the native stage (with OpenCppCoverage if installed on Windows, otherwise plain `ctest`), then the two Node stages (`--test-force-exit`) through a wrapper that force-kills `node --test` after **20 minutes** — a backstop against a hung run, far above the couple of minutes the integration suite takes, so `npm test` is a valid one-shot check. The stages can also be run on their own: `npm run test:unit` and `npm run test:integration`.

### Gotchas

- **The TypeScript tests import the compiled library from `dist/lib`.** After changing `lib/`, run `npm run build:lib` first, or you are testing stale code.
- **Windows will not overwrite a running `.exe`.** A leftover `themisto.exe` / `elara.exe` / `carpo.exe` (a crashed test run, a playground left open) makes the next native build fail at link time (`LNK1104`) and can silently slow or hang later runs. Kernels are put in a job object that dies with Themisto, but a supervisor that outlives its test runner still keeps them. Check with `tasklist | findstr /i "themisto elara carpo"` and kill strays before building. To run something while rebuilding, point `JOVIAN_NATIVE_DIR` at a *copy* of `dist/native/Release`.
- **Linux (Ubuntu/Debian).** The suites were run on Ubuntu 26.04 under WSL (native 12/12, unit, integration). What that setup needed: `cmake ninja-build uuid-dev r-base-dev python3-venv` from apt (`python3-venv` is for CarpoTest's venv test); the **official Node** from nodejs.org, because the distribution's Node has no TypeScript type stripping (`ERR_UNKNOWN_FILE_EXTENSION` on `.ts`); and `hera`'s dependencies from CRAN in a private `R_LIBS_USER` with `R_LIBS_SITE=/nonexistent`, since the apt `r-cran-*` packages fail to load (`undefined symbol: SETLENGTH`, built for a different R ABI). Export those two variables in the shell that runs `ctest` and the Node tests so the kernel processes inherit them. Build in the WSL filesystem (rsync the tree), not under `/mnt/c`. macOS is covered only by CI.
- **A test that holds a mutex its own `onMessage` callback needs, while calling `stopSession()`, deadlocks:** the supervisor's poll thread now keeps relaying (the kernel's `shutdown_reply`) during the stop. Scope such locks.
- **Every real-kernel test starts a process** (~0.7 s locally, more on cold CI runners) — keep the number of sessions per test small.
- `execute()` rejections and `'error'` events: tests that create a `Session` must attach an `'error'` listener or the Node process dies (see [API](api/README.md#events)).

## CI

`.github/workflows/ci.yml` runs on every push to `main`, on pull requests, and manually, on **Windows, Ubuntu and macOS** (`fail-fast: false`):

1. Checkout, Node (`lts/*`), R (`release`, without Rtools on Windows), Python (`3.x`).
2. `r-lib/actions/setup-r-dependencies` with `packages: local::packages/hera` — installs `hera` and all its CRAN imports (real code execution needs it).
3. `uuid-dev` on Linux.
4. Clone and bootstrap vcpkg (`VCPKG_ROOT`), with the vcpkg binary cache keyed on `vcpkg.json`.
5. Configure + build native (Release): elara, themisto, carpo.
6. Configure + build the native tests (`dist/native-test`, `-DJOVIAN_BUILD_TESTS=ON`) and run `ctest -C Release --output-on-failure --timeout 180`.
7. `npm ci --legacy-peer-deps` (a known peer-dependency conflict between TypeScript 7 and the `@typescript-eslint` plugin), `npx tsc --build`, unit tests, integration tests.

## Releasing

The npm packages are built and published by `.github/workflows/release.yml` from a version tag; see [releasing.md](releasing.md).

## Debugging

- **Kernel logs.** Everything a kernel prints (`[R Interpreter] …`, `[carpo] …`, and anything R/Python writes outside an execution) appears on the *supervisor's* stderr, which `lib/` forwards to your process's stderr, prefixed `[elara]` / `[carpo]`. That is the first place to look when a session fails to start.
- **Library logs.** Pass `logger` to `createSession()` (or read the default console output): `trace` shows queueing, request ids and timeouts.
- **Run a kernel by hand.** Generate a kernelspec (`npm run jupyter:kernelspec`) and start it from `jupyter console --kernel elara`, or run `elara -f <connection-file> --r-home …` yourself — no supervisor involved.
- **Talk to the supervisor directly.** Start `themisto` (it prints `{"type":"supervisorReady","httpPort":…,"wsPort":…}`) and use `curl` against [its HTTP API](protocol.md#2-themistos-http-api) and any WebSocket client against `ws://127.0.0.1:<wsPort>/sessions/<id>/messages`.
- **One native test.** `dist/native/Release/session_registry_test.exe --gtest_filter=*Interrupt*` (kernel log noise goes to the same stdout; filter with `grep -v "^\[elara\]"`).
- **Crashes.** A kernel crash is reported with its decoded exit code (`0xc0000005` = access violation). The Windows Event Viewer's Application log usually has the faulting module.
- **Hangs in native tests** are almost always a leaked lock or a socket used from two threads: ZMQ sockets are not thread-safe, which is why `DealerChannel`, the control socket and iopub publishing carry mutexes.

## Style and tooling

`npm run format` / `npm run format:check` (clang-format) and `npm run lint` (`eslint lib/**/*.ts`, `clang-tidy native/src/**/*.cpp`) are wired in `package.json`; no ESLint or clang-format configuration file is currently checked in, so they use defaults (or your own config). Code comments in this repo explain *why* (constraints, past bugs), not what.

## Adding a kernel

See [C++ usage](cpp-usage.md#writing-a-new-interpreter-a-new-language-kernel): implement `adrastea::Interpreter`, register it, write a `main()`, add a CMake target, and add its executable to Themisto's `kernelExePaths` map (`native/src/themisto/main.cpp`) under a new `kernelType`.
