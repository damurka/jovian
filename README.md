# Jovian

[![CI](https://github.com/damurka/jovian/actions/workflows/ci.yml/badge.svg)](https://github.com/damurka/jovian/actions/workflows/ci.yml)

Jovian runs **R** and **Python** as supervised Jupyter kernels that you can drive from Node.js and Electron. Every session is its own operating-system process with its own embedded interpreter, so one session blocking on a long call (a Shiny app, a slow loop) never starves another, and a kernel that crashes takes only its own session with it.

```typescript
import { SessionManager } from '@damurka/jovian';

const manager = new SessionManager();

const r = await manager.createSession({ kernelType: 'r', rHome: process.env.R_HOME, workingDirectory: '/projects/analysis' });
const result = await r.execute('x <- 1:10; mean(x)');
console.log(result.success, result.output);

const py = await manager.createSession({ kernelType: 'python', pythonHome: '/path/to/python' });
console.log((await py.execute('sum(range(1, 11))')).success);

await manager.stopAll();
```

The pieces are named after moons of Jupiter:

| Name | Role |
|---|---|
| **Jovian** | This npm package: a TypeScript client (`lib/`) over the native binaries below. |
| **Adrastea** | Language-neutral Jupyter kernel framework — wire protocol, ZMQ transport, request loop, the abstract `Interpreter` interface (`native/`, a static library). |
| **Elara** | The R kernel: embeds R on top of Adrastea (`elara` / `elara.exe`). |
| **Carpo** | The Python kernel: embeds CPython on top of Adrastea the same way (`carpo` / `carpo.exe`). |
| **Themisto** | The kernel supervisor: spawns and monitors one kernel process per session and re-exposes sessions over HTTP + WebSocket (`themisto` / `themisto.exe`). |
| [hera](packages/hera) | The R companion package loaded inside every Elara session (execution, completion, inspection, comms). |

## Install

```bash
npm install @damurka/jovian
```

The package ships **prebuilt** `themisto`, `elara` and `carpo` binaries — no compiler, CMake or vcpkg — for **Windows x64** (`win32-x64`) and **Linux x64** (`linux-x64`); macOS and Linux on ARM are not published yet, so build from source there and point `JOVIAN_NATIVE_DIR` at `dist/native/Release`. npm installs the matching `@damurka/jovian-<os>-<cpu>` package automatically as an optional dependency, so do not install with `--omit=optional` / `--no-optional`. Node.js ≥ 22.4 is required (ES modules; global `fetch` and `WebSocket`).

What you must already have on the machine:

- **R** (4.2 or newer; a build with a shared library, which the CRAN/Posit binaries and distribution packages are) for R sessions. Pass its location as `rHome` (`R RHOME` prints it). The `hera` R package that every R session needs ships inside the npm package and is installed into R on a session's first start, which needs the `remotes` package and `hera`'s CRAN dependencies (`cli`, `evaluate`, `glue`, `IRdisplay`, `jsonlite`, `R6`, `repr`, `rlang`):

  ```r
  install.packages(c("remotes", "cli", "evaluate", "glue", "IRdisplay", "jsonlite", "R6", "repr", "rlang"))
  ```

- **Python 3** with its shared library (optional, for Python sessions); pass `pythonHome` (`python3 -c "import sys; print(sys.prefix)"`).
- **Linux:** `libuuid` (`libuuid1`, present on nearly every system) and a glibc at least as new as the one the binaries were built against (Ubuntu 24.04's, 2.39). On an older distribution, [build from source](#requirements).
- **Windows:** the [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist) (x64, 2015–2022) — the binaries use the dynamic C++ runtime; most machines already have it.

The rest of this README is for building Jovian from source.

## Features

- **Two kernels, one API.** R (Elara) and Python (Carpo), chosen per session with `kernelType`.
- **Execution** with streaming stdout/stderr, `execute_result`, rich `display_data` (R plots arrive as `image/png`), `update_display_data`, `clear_output`, and structured errors with tracebacks.
- **Interactive input.** `input()` (Python) and `readline()` (R) round-trip over the Jupyter stdin channel; see [the input guide](docs/guides/interactive-input.md).
- **Real interrupt.** `session.interrupt()` breaks running R and Python code (a sleeping call, a busy loop), not just the queue; the kernel keeps running afterwards. See [interrupting](docs/guides/interrupting.md).
- **Working directory.** `workingDirectory` is where the kernel process starts (`getwd()` / `os.getcwd()`); it survives restarts.
- **Protocol requests as methods.** `complete`, `inspect`, `isComplete`, `kernelInfo`, `commInfo`, `queryKernelHistory` — real Jupyter messages, answered by the kernel. See [API reference](docs/api/README.md).
- **Comms.** Open a comm to a kernel-side target, or receive kernel-initiated ones with `session.on('comm', ...)`. See [comms](docs/guides/comms.md).
- **`user_expressions` and `stop_on_error`.** Evaluate expressions after a cell succeeds; abort everything queued behind a failing cell.
- **Protocol-driven lifecycle.** `stop()` sends the kernel a real `shutdown_request` (`restart: false`), `restart()` one with `restart: true`; the kernel's `shutdown_reply` surfaces as an event and an orderly exit is never reported as a crash.
- **Crash detection in milliseconds.** The supervisor watches the OS process handle, with the ZMQ heartbeat as a backstop for a kernel that is alive but stuck. A crashed session can be `restart()`ed in place under the same session id.
- **Restart with different options.** Switch R or Python installations on restart; unchanged options are kept ([details](docs/guides/sessions-lifecycle.md)).
- **A browser playground** (Next.js) for exercising live sessions — see [`tools/playground`](tools/playground/README.md).
- **Standard Jupyter launch mode.** `elara` and `carpo` can also be started directly by `jupyter lab` / `jupyter console` via a generated kernelspec (`npm run jupyter:kernelspec`), no supervisor involved.

## Requirements

*Building from source. To use the published package, see [Install](#install).*

Jovian builds C++ (Adrastea, Elara, Carpo, Themisto) and TypeScript. R and Python are **runtime** dependencies of the kernels, not build-time ones: Elara and Carpo load R's and Python's shared libraries dynamically when a session starts, so the binaries build without either installed (R's headers are still needed to compile Elara).

### All platforms

| Requirement | Notes |
|---|---|
| **Git** | To clone the repo and vcpkg. |
| **CMake ≥ 3.24** | Declared in `CMakeLists.txt`. The local build uses CMake 4.3.1 (the copy bundled with Visual Studio 2026). |
| **A C++23-capable compiler** | `CMAKE_CXX_STANDARD 23` is required. See the platform sections for what is actually verified. |
| **vcpkg** with `VCPKG_ROOT` set | Dependencies come from the manifest in `vcpkg.json` (pinned `builtin-baseline`): `nlohmann-json`, `cppzmq`, `zeromq`, `openssl`, `gtest`, `cpp-httplib`, `ixwebsocket`. Clone [microsoft/vcpkg](https://github.com/microsoft/vcpkg) and run its bootstrap script (`bootstrap-vcpkg.bat` / `bootstrap-vcpkg.sh`); CMake picks the manifest up through the toolchain file `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. |
| **Node.js** (recent LTS) | The library is ES modules compiled with `tsc`; the test files are `.ts` and are run directly with `node --test`, which needs a Node with built-in TypeScript type stripping (unflagged since Node 22.18 / 23.6). Developed on Node 24; CI uses the current LTS. Use the official build from nodejs.org: some distribution packages (Ubuntu's `nodejs`) omit type stripping and fail with `ERR_UNKNOWN_FILE_EXTENSION` on `.ts` files. Global `fetch` and `WebSocket` are used by the client. |
| **R** (to run R sessions, and its headers to build Elara) | Developed against R 4.6.0; CI uses the latest release. On Windows, R ≥ 4.2 is needed for `readline()` to work over the stdin channel (older R still starts, but `readline()` cannot be answered). |
| **The R package `hera`** and its dependencies | Every R session needs it. Its `Imports` (from `packages/hera/DESCRIPTION`) are `cli`, `evaluate`, `glue`, `IRdisplay`, `jsonlite`, `R6`, `repr`, `rlang`, `tools`, `utils`. CI installs them with `r-lib/actions/setup-r-dependencies` using `packages: local::packages/hera`; locally, install those packages from CRAN and run **`npm run hera:install`** (`R CMD INSTALL packages/hera`), or pass `heraSrcPath` so Elara installs it on first start (needs the `remotes` package; `heraSrcPath` has no default). Use `hera` >= 0.6.0.9001: earlier versions work but only show the output of one long-running R expression when it ends, not as it is produced — re-run `npm run hera:install` after pulling. |
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
- CI runs on `ubuntu-latest`. The full suite (native, unit, integration) was also run on **Ubuntu 26.04 under WSL**; notes for Debian/Ubuntu:
  - `cmake/FindR.cmake` finds Debian's split R headers (`R.h` in `/usr/share/R/include`) by asking `R CMD config --cppflags`, so `r-base-dev` is enough.
  - Carpo looks for libpython in `lib/`, `lib64/` and `lib/<arch>-linux-gnu/`, so a distribution Python works with `PYTHONHOME=/usr` (`sudo apt install python3`; add `python3-venv` to run CarpoTest's venv test).
  - If `hera` fails to install with `undefined symbol: SETLENGTH`, the apt `r-cran-*` packages were built for a different R ABI: hide the site library (`R_LIBS_SITE=/nonexistent`) and install `hera`'s dependencies from CRAN into a private `R_LIBS_USER`, as CI does.
  - The distribution's packaged Node.js has no TypeScript type stripping; use the official build from nodejs.org to run the `.ts` tests.
  - macOS is covered only by CI.

### macOS

- A C++23-capable Apple clang (Xcode or the Command Line Tools); the build links the CoreFoundation framework. CI runs on `macos-latest` using the runner's preinstalled toolchain and installs nothing extra.
- R must provide `libR.dylib` under `$R_HOME/lib` (CRAN's framework build does).

### Environment variables

| Variable | Used by | Meaning |
|---|---|---|
| `VCPKG_ROOT` | build | vcpkg checkout; used by `npm run build` and the CMake presets. |
| `R_HOME` | runtime, tests, examples | R installation to use when `rHome` is not passed. `R RHOME` is used as a fallback by the tests and the playground. |
| `R_PATH`, `R_LIBS` | examples, playground | Passed as `rPath` / `rLibs`. |
| `PYTHONHOME` | runtime, tests | Python installation prefix when `pythonHome` is not passed. |
| `JOVIAN_NATIVE_DIR` | `lib/` | Directory holding `themisto`, `elara` and `carpo`. Default: the installed `@damurka/jovian-<os>-<cpu>` package, else `dist/native/Release` in a source checkout. Use it to run against a *copy* of the binaries (Windows will not let you overwrite a running `.exe`). |
| `ELARA_HERA_SRC` | Elara | Set for you from the `heraSrcPath` option: where Elara installs `hera` from if it is missing or older than the source. |

## Building and testing

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

The TypeScript tests import the **compiled** library from `dist/`, so run `npm run build:lib` after changing anything in `lib/`. The integration tests skip themselves when the native binaries are not built, and the Python ones skip when no Python or no `carpo` is available. More in [docs/development.md](docs/development.md).

Other scripts: `npm run hera:install` (install/update the `hera` R package from `packages/hera`), `npm run dev` (TypeScript watch mode), `npm run clean` (removes `dist/`), `npm run format`, `npm run lint`, `npm run jupyter:kernelspec` (write `kernel.json` files for `elara` and `carpo`; `-- --only=r` / `--only=python` for one).

## Playground

A browser UI (Next.js) for driving real R and Python sessions, with completion (`Tab`) and inspection (`Shift+Tab`, double-click in the output):

```sh
npm run playground:install   # once — the playground has its own node_modules
npm run playground           # http://127.0.0.1:4173
```

See [`tools/playground/README.md`](tools/playground/README.md).

## Documentation

| | |
|---|---|
| [Getting started](docs/getting-started.md) | Prerequisites → build → your first R and Python session. |
| [Architecture](docs/architecture/overview.md) | Components, process and thread model, message flows. |
| [Protocol](docs/protocol.md) | Jupyter messages supported, Themisto's HTTP API and WebSocket frames. |
| [API reference](docs/api/README.md) | `SessionManager`, `Session`, `Comm`, options and result types. |
| Guides | [Interactive input](docs/guides/interactive-input.md) · [Interrupting](docs/guides/interrupting.md) · [Comms](docs/guides/comms.md) · [History](docs/guides/history.md) · [Session lifecycle](docs/guides/sessions-lifecycle.md) · [R and Python environments](docs/guides/environments.md) · [Playground](docs/guides/playground.md) |
| [Kernels](docs/kernels.md) | How Elara and Carpo work, and where they differ. |
| [Development](docs/development.md) | Repo layout, build system, test layers, CI, debugging. |
| [Releasing](docs/releasing.md) | How the npm packages are built and published. |
| [Troubleshooting](docs/troubleshooting.md) | Real error messages and what causes them. |
| [C++ usage](docs/cpp-usage.md) | Using Adrastea from C++; writing a new language kernel. |

Runnable examples are in [`examples/`](examples): [`basic/simple-execute.js`](examples/basic/simple-execute.js) (one session) and [`advanced/two-sessions.js`](examples/advanced/two-sessions.js) (two concurrent sessions, one blocking).

## Status

Both kernels support execute, streaming output, display data, errors, completion, inspection, `is_complete`, kernel info, history, comms, interactive input, real interrupt, and session restart / crash recovery. Elara additionally has a standard Jupyter connection-file launch mode alongside the Themisto-supervised one; Carpo's kernelspec is generated too but has not been exercised against a real `jupyter lab` / `jupyter console`. Carpo supports pointing a session at a venv's `site-packages`. Interrupt was verified on Windows; its POSIX implementation (`pthread_kill` of SIGINT to the interpreter thread) is covered by the same tests but relies on CI to run them on Linux and macOS.
