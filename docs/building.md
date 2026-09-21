# Building from source

You do not need this to use Jovian from Node.js: `npm install @damurka/jovian` brings prebuilt binaries for Windows x64, Linux x64 and arm64, and macOS x64 and arm64 (see the [README](../README.md#install)). Build from source to work on Jovian itself, or to run it on a platform without a prebuilt package (Windows on ARM, 32-bit ARM Linux, other Unixes) — see [Using your own build](#using-your-own-build).

Jovian builds C++ (Adrastea, Elara, Carpo, Themisto) and TypeScript. R and Python are **runtime** dependencies of the kernels, not build-time ones: Elara and Carpo load R's and Python's shared libraries dynamically when a session starts, so the binaries build without either installed (R's headers are still needed to compile Elara).

## Requirements

### All platforms

| Requirement | Notes |
|---|---|
| **Git** | To clone the repo and vcpkg. |
| **CMake ≥ 3.24** | Declared in `CMakeLists.txt`. The local build uses CMake 4.3.1 (the copy bundled with Visual Studio 2026). |
| **A C++23-capable compiler** | `CMAKE_CXX_STANDARD 23` is required. See the platform sections for what is actually verified. |
| **vcpkg** with `VCPKG_ROOT` set | Dependencies come from the manifest in `vcpkg.json` (pinned `builtin-baseline`): `nlohmann-json`, `cppzmq`, `zeromq`, `openssl`, `gtest`, `cpp-httplib`, `ixwebsocket`. Clone [microsoft/vcpkg](https://github.com/microsoft/vcpkg) and run its bootstrap script (`bootstrap-vcpkg.bat` / `bootstrap-vcpkg.sh`); CMake picks the manifest up through the toolchain file `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. |
| **Node.js** (24, or any recent LTS) | The library is ES modules compiled with `tsc`; the test files are `.ts` and are run directly with `node --test`, which needs a Node with built-in TypeScript type stripping (unflagged since Node 22.18 / 23.6). Developed on Node 24; CI uses 24. Use the official build from nodejs.org (or nvm): some distribution packages (Ubuntu's `nodejs`) omit type stripping and fail with `ERR_UNKNOWN_FILE_EXTENSION` on `.ts` files. Global `fetch` and `WebSocket` are used by the client. |
| **R** (to run R sessions, and its headers to build Elara) | Developed against R 4.6; CI uses the latest release. On Windows, R ≥ 4.2 is needed for `readline()` to work over the stdin channel (older R still starts, but `readline()` cannot be answered). |
| **The R package `hera`** and its dependencies | Every R session needs it. Its `Imports` (from `packages/hera/DESCRIPTION`) are `cli`, `evaluate`, `glue`, `IRdisplay`, `jsonlite`, `R6`, `repr`, `rlang`, `tools`, `utils`. In a source checkout nothing installs it for you: install those packages from CRAN and run **`npm run hera:install`** (`R CMD INSTALL packages/hera`). CI does the same with `r-lib/actions/setup-r-dependencies` and `packages: local::packages/hera`. Alternatively pass `heraSrcPath: 'packages/hera'` to `createSession()`: before the first R session the library then installs `hera` and its dependencies itself, exactly as it does for an npm install. Use `hera` >= 0.6.0.9001: earlier versions work but only show the output of one long-running R expression when it ends, not as it is produced — re-run `npm run hera:install` after pulling. |
| **Python** (optional) | Only needed to run Python sessions and to run `CarpoTest`. Carpo does not include Python's headers or link Python at build time. A CPython 3 installation with its shared library: `python3NN.dll` on Windows, `libpython3.*.so` on Linux, `libpython3.*.dylib` on macOS. |

### Windows

- **Visual Studio with the “Desktop development with C++” workload** (MSVC, Windows SDK, and the bundled CMake). The repository is built and tested here with **Visual Studio 2026 (version 18), MSVC toolset v145**, generator `Visual Studio 18 2026`. CI builds on the GitHub `windows-latest` runner. Earlier Visual Studio versions have not been tried; the build needs MSVC's `/std:c++latest` C++23 mode.
- `CMakePresets.json` also defines Ninja + `cl.exe` presets (`x64-debug`, `x64-release`, `x86-*`); they require `VCPKG_ROOT` and `VSINSTALLDIR` (open a *Developer PowerShell / Command Prompt*, or configure from Visual Studio).
- vcpkg installs the `x64-windows` triplet (dynamic DLLs), which is why `dist/native/Release/` contains ZeroMQ/OpenSSL DLLs next to the executables.
- R's DLL lives in `<R_HOME>\bin\x64`; pass it as `rPath` if it is not on `PATH`. You do **not** need Rtools.
- Note that `npm run compile` uses `%VCPKG_ROOT%` (cmd.exe syntax); use `npm run build` from any shell.

### Linux

- A C++23 compiler, `cmake`, and vcpkg's own prerequisites (see the vcpkg docs). CI installs one extra package: **`uuid-dev`** (`sudo apt-get install uuid-dev`), because the GUID code calls `uuid_generate()` from libuuid.
- R **must have been built with a shared library** (`--enable-R-shlib`; distribution packages and the CRAN/Posit binaries are). Elara `dlopen`s `$R_HOME/lib/libR.so`.
- CI runs on `ubuntu-24.04` and `ubuntu-24.04-arm`. The full suite (native, unit, integration) was also run on **Ubuntu 26.04 under WSL**; notes for Debian/Ubuntu:
  - `cmake/FindR.cmake` finds Debian's split R headers (`R.h` in `/usr/share/R/include`) by asking `R CMD config --cppflags`, so `r-base-dev` is enough.
  - Carpo looks for libpython in `lib/`, `lib64/` and `lib/<arch>-linux-gnu/`, so a distribution Python works with `PYTHONHOME=/usr` (`sudo apt install python3`; add `python3-venv` to run CarpoTest's venv test).
  - If `hera` fails to install with `undefined symbol: SETLENGTH`, the apt `r-cran-*` packages were built for a different R ABI: hide the site library (`R_LIBS_SITE=/nonexistent`) and install `hera`'s dependencies from CRAN into a private `R_LIBS_USER`, as CI does. (The library's automatic setup reinstalls such packages itself; see [Troubleshooting](troubleshooting.md).)
  - The distribution's packaged Node.js has no TypeScript type stripping; use the official build from nodejs.org to run the `.ts` tests.

### macOS

- A C++23-capable Apple clang (Xcode or the Command Line Tools); the build links the CoreFoundation framework. CI runs on `macos-latest` (Apple Silicon) and `macos-15-intel` using the runner's preinstalled toolchain and installs nothing extra. It sets `MACOSX_DEPLOYMENT_TARGET=14.0`: the code uses `std::format` on floating-point values, which needs macOS 13.3 or newer at run time.
- R must provide `libR.dylib` under `$R_HOME/lib` (CRAN's framework build does).

## Build and test

```sh
npm install --legacy-peer-deps   # CI uses `npm ci --legacy-peer-deps` (typescript and the eslint plugin disagree on peer versions)
npm run build                    # native (elara + carpo + themisto, Release) into dist/native, then TypeScript into dist/lib
```

`npm run build` configures with `VCPKG_ROOT`'s toolchain file when the variable is set. The individual steps are `npm run build:native` (only `cmake --build`, after a configure) and `npm run build:lib` (`tsc --build`).

```sh
npm test                 # native ctest + TypeScript unit + integration tests (scripts/test.js)
npm run test:unit        # TypeScript unit tests only (fake WebSocket, no processes)
npm run test:integration # end-to-end: real themisto + real R / Python kernels
```

The TypeScript tests import the **compiled** library from `dist/`, so run `npm run build:lib` after changing anything in `lib/`. The integration tests skip themselves when the native binaries are not built, and the Python ones skip when no Python or no `carpo` is available. More in [Development](development.md).

Other scripts: `npm run hera:install` (install/update the `hera` R package from `packages/hera`), `npm run dev` (TypeScript watch mode), `npm run clean` (removes `dist/`), `npm run format`, `npm run lint`, `npm run jupyter:kernelspec` (write `kernel.json` files for `elara` and `carpo`; `-- --only=r` / `--only=python` for one), `npm run playground` (see the [playground guide](guides/playground.md)).

## Environment variables

Variables that matter when building and developing (the ones for *using* the package are in the [README](../README.md#environment-variables)):

| Variable | Used by | Meaning |
|---|---|---|
| `VCPKG_ROOT` | build | vcpkg checkout; used by `npm run build` and the CMake presets. |
| `R_HOME` | tests, examples | R installation for the tests and examples when `rHome` is not passed. `R RHOME` is the fallback. |
| `R_PATH`, `R_LIBS` | examples, playground | Passed as `rPath` / `rLibs`. |
| `PYTHONHOME` | tests | Python installation prefix for the tests when `pythonHome` is not passed. |
| `JOVIAN_NATIVE_DIR` | `lib/` | Directory holding `themisto`, `elara` and `carpo`. Use it to run against a *copy* of the binaries while rebuilding (Windows will not let you overwrite a running `.exe`), or to point the library at your own build. |
| `ELARA_HERA_SRC` | Elara | Set for you from the `heraSrcPath` option: where Elara installs `hera` from if it is missing or older than the source. |

## Using your own build

On a platform with no prebuilt package the library says so (`jovian: there are no prebuilt kernels for <os>-<cpu>`). Build here, then point it at the output:

```sh
npm run build
JOVIAN_NATIVE_DIR=/path/to/jovian/dist/native/Release node your-app.js
```

`dist/native/Release` needs `themisto`, `elara` and (for Python sessions) `carpo`, plus on Windows the DLLs next to them. `scripts/release.mjs platform --version X` shows exactly which files a platform package takes from that directory.
