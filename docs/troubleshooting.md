# Troubleshooting

Messages below are quoted from the code. **First habit:** a kernel that fails to start reports only a generic error to your code; the real cause is on stderr, prefixed `[elara]` (R) or `[carpo]` (Python) — the supervisor re-prints everything the kernel writes. Read that output first.

## Building

| Symptom | Cause / fix |
|---|---|
| CMake cannot find `nlohmann_json` / `zeromq` / `cppzmq` / `httplib` / `ixwebsocket` | vcpkg's toolchain was not used. Set `VCPKG_ROOT` and re-configure with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` (`npm run build` does this when `VCPKG_ROOT` is set). |
| `FindR.cmake requires the following variables to be set: R_COMMAND` | `cmake/FindR.cmake` runs `R RHOME`; put `R` on `PATH` or pass `-DR_COMMAND=<path to R>`. Only R's *headers* are needed to build. |
| `R.h: No such file or directory` (Debian/Ubuntu) | Install `r-base-dev`. `cmake/FindR.cmake` finds Debian's split headers (`R.h` in `/usr/share/R/include`) via `R CMD config --cppflags`; if your R reports no such directory, add it with `-DCMAKE_CXX_FLAGS=-I<dir containing R.h>`. |
| `ERR_UNKNOWN_FILE_EXTENSION … ".ts"` running the tests | The Node build has no TypeScript type stripping (Ubuntu's packaged `nodejs`). Use the official build from nodejs.org. |
| `undefined reference to uuid_generate` (Linux) | Install `uuid-dev` (`sudo apt-get install uuid-dev`). |
| `LNK1104: cannot open file '…\themisto.exe'` (Windows) | A `themisto` / `elara` / `carpo` process is still running from an earlier run. Kill strays (`tasklist \| findstr /i "themisto elara carpo"`) — or run against a *copy* of `dist/native/Release` via `JOVIAN_NATIVE_DIR`. |
| C++23 / `/std:c++latest` errors on Windows | Use Visual Studio 2026 (the toolset the repo is built with); older toolsets are untested. |
| `npm ci` fails on peer dependencies | Use `--legacy-peer-deps` (as CI does): TypeScript 7 conflicts with the `@typescript-eslint` plugin's declared range. |
| Unit tests fail after editing `lib/` | The tests import `dist/lib`: run `npm run build:lib`. |

## Creating a session

| Message | Meaning and fix |
|---|---|
| `jovian: the kernel binaries were not found. Expected the '@damurka/jovian-<os>-<cpu>' package …` | The platform package was not installed: it is an optional dependency, so `npm install --omit=optional` / `--no-optional` skips it (reinstall without the flag), or a lockfile made on another OS omitted it (`npm install` on this OS). In a source checkout: run `npm run build`, or set `$JOVIAN_NATIVE_DIR`. |
| `jovian: there are no prebuilt kernels for <os>-<cpu> (supported: …)` | Prebuilt packages exist for `win32-x64`, `linux-x64`, `linux-arm64`, `darwin-arm64`, `darwin-x64`. Elsewhere, [build from source](../README.md#requirements) and set `$JOVIAN_NATIVE_DIR` to `dist/native/Release`. |
| `version 'GLIBC_2.xx' not found` on Linux | The prebuilt Linux binaries need a glibc at least as new as the one they were built with (Ubuntu 24.04: 2.39). Build from source on the older system. |
| `Supervisor process exited before it was ready (code 1)` | `themisto` refused to start — typically `[themisto] FATAL: kernel executable not found at … (pass --kernel-exe to override)`: `elara` is missing next to `themisto`. |
| `no kernel executable is configured for kernelType 'python' …` | `carpo` was not built (or not found beside `themisto`). It is built by default; check `dist/native/Release/carpo[.exe]` and Themisto's `[themisto] NOTE: no Python kernel executable found` line. R sessions are unaffected. |
| `workingDirectory does not exist or is not a directory: <path>` | Create the directory first, or fix the path. |
| `Kernel process exited before it could register -- check its stderr output for the actual error.` | The kernel crashed at start-up. See the R / Python sections below and read the `[elara]` / `[carpo]` lines. |
| `Did not receive kernel configuration within 60s -- the kernel process is still running but never registered. Check its stderr output for what it's doing.` | The kernel started but hung before registering — e.g. R blocked loading packages, a very slow disk, or a security product scanning the process. The stderr shows how far it got. |
| `WebSocket connection to session <id> failed` / `Session <id> closed before it was ready` | The supervisor accepted the session but the WebSocket could not be established/kept — usually the supervisor died. Look for its exit. |

### R

| Symptom | Cause / fix |
|---|---|
| `Could not load R.dll (…). Is R installed? Checked PATH and R_HOME=… Install R from https://cran.r-project.org, or make sure R_HOME/the R bin directory is configured correctly.` | Windows: `rHome` wrong, or `rPath` (the folder containing `R.dll`, normally `<R_HOME>\bin\x64`) is not right. |
| `R_HOME is not set -- elara needs a working R installation to run. …` | Linux/macOS: pass `rHome` (find it with `R RHOME`). |
| `Could not load …/lib/libR.so (…). Is R installed at '…'? If this R was built from source, it needs to have been configured with --enable-R-shlib, or no libR.so exists at all` | Use a distribution/CRAN R, or rebuild R with `--enable-R-shlib`. |
| No `libR.dylib` on macOS | Point `rHome` at the framework's `Resources` directory (what `R RHOME` prints). |

#### `hera` is not installed

Symptoms: the kernel log says `WARNING: 'hera' package could not be loaded (status: …)`, then `execute()` results in an error — the reply carries `R evaluation of hera:::hera_call("execute", ...) failed (is the 'hera' package installed?): …`, and completion/inspect fail the same way.

On Debian/Ubuntu, `R CMD INSTALL` may fail loading an apt package with `undefined symbol: SETLENGTH` (`rlang`, `vctrs`, `htmltools`, …): those `r-cran-*` packages were built for a different R ABI. Hide the site library and install from CRAN into a private one — `export R_LIBS_SITE=/nonexistent R_LIBS_USER=$HOME/Rlib`, then `install.packages()` the dependencies and run `npm run hera:install` in the same shell, keeping both variables set when you start sessions.

Fix: install `hera` and its dependencies into the library the session uses (`npm run hera:install`, i.e. `R CMD INSTALL packages/hera`, after installing `cli`, `evaluate`, `glue`, `IRdisplay`, `jsonlite`, `R6`, `repr`, `rlang` from CRAN), or pass `heraSrcPath: '<repo>/packages/hera'` (needs `remotes`) so Elara installs it. The status names in the warning: `no_source_configured` (not installed and no `heraSrcPath`), `source_not_found` (path does not exist), `remotes_unavailable` (install `remotes`), `install_failed` (see R's error above it — usually a missing dependency or an unwritable library: set `rLibs`).

There is **no bundled fallback**: `heraSrcPath` has no default, whatever older comments say.

#### R code fails after the kernel started

- `there is no package called '…'` — the package is not in `rLibs` / the default library of that R installation.

### Python

| Message | Cause / fix |
|---|---|
| `No python3NN.dll was found directly under python_home ('…'). Is Python installed there? …` | Windows: `pythonHome` must be the install root that directly contains `python3NN.dll`. |
| `No libpython3.*.so*/.dylib was found under '<home>/lib' (or lib64/, lib/<arch>-linux-gnu/). …` | POSIX: `pythonHome` must be the prefix whose `lib/`, `lib64/` or `lib/<arch>-linux-gnu/` has the shared library (a distribution Python works with `pythonHome: '/usr'`). Some builds (pyenv without `--enable-shared`, some Homebrew/conda layouts) have none. |
| `Could not load <path> (…)` | The library was found but would not load — a 32/64-bit mismatch or a missing dependency. |
| Imports from your venv fail | Set `venvPath` **and** keep `pythonHome` at the base install (`sys.base_prefix`). Inside a venv, `sys.prefix` has neither libpython nor the standard library. |

## Running code

| Symptom | Cause / fix |
|---|---|
| `Execution timed out after 30000ms (the kernel was interrupted)` | The default `timeout` fired. The library interrupted the kernel, so it is usable again straight away. Raise the timeout (`execute(code, { timeout })`) or use `0` for none. With `interruptOnTimeout: false` the message has no parenthesis and the kernel is **still running** the code: `interrupt()` or restart it, or new work — and `complete()` / `inspect()` — queue behind it and look hung. |
| `complete()` / `inspect()` / `kernelInfo()` time out (10 s) | The kernel is busy: it answers requests on the thread that runs code, so they wait until the running execution ends (only `interrupt()` is answered mid-run). Usually a long or timed-out execution left running (`interruptOnTimeout: false`, or a client that gave up without interrupting). `interrupt()` it. |
| Output of one long R expression (a `for` loop of `print()`s) only appears when it finishes | An older `hera` is installed. Live streaming inside a single top-level expression needs `hera` >= 0.6.0.9001: run `npm run hera:install` (see [Environments](guides/environments.md#the-hera-package-required)). |
| `Queue cleared` | The session was stopped, restarted, killed or crashed while your `execute()` was queued or running. |
| `Execution queue is full` | More than `queueSize` (100) queued calls. |
| `Execution aborted: an earlier execution failed with stopOnError` | Expected: a previous call with `stopOnError` failed (`result.aborted === true`). |
| `This execution didn't allow interactive input (allow_stdin was false) …` | Pass `{ allowStdin: true }` and answer `'input_request'` — see [Interactive input](guides/interactive-input.md). |
| The process crashes with `Unhandled 'error' event` | You did not attach `session.on('error', …)`. Node throws for `'error'` without a listener. |
| A script never exits | Call `await manager.stopAll()` — the supervisor's pipes keep the event loop alive. |
| `execute()` never resolves while a Shiny app runs | `shiny::runApp()` blocks the kernel; use `createShiny()` (which passes `timeout: 0`) and `interrupt()` to stop it. |
| `interrupt()` returned `false` | The kernel did not answer within 5 s: it is blocked waiting for `input()`, stuck in native code, or dead. See [Interrupting](guides/interrupting.md#limits). |
| A request (`complete`, `kernelInfo`, …) times out after 10 s | The kernel is busy running code — requests other than interrupt wait for the running execution. |
| `Timed out waiting for a <x>_reply after <n>ms` | Same, or the kernel is hung. |
| Comm messages seem lost | Attach the `'comm'` listener before running the R code that opens the comm; register the target before `new_comm()` (an unregistered target yields `NULL`). Comms only exist in R sessions. |

## Crashed and unresponsive sessions

| You see | Meaning |
|---|---|
| `'exit'` with `kernel process exited unexpectedly (process exited with code 0xc0000005 (STATUS_ACCESS_VIOLATION -- a native crash, e.g. in a compiled R package))` | The kernel process died. Common codes are decoded (`STATUS_STACK_OVERFLOW`, `STATUS_STACK_BUFFER_OVERRUN`). `await session.restart()` recovers the session under the same id. |
| `'exit'` with `heartbeat gave up waiting for a response (…)` | The process is alive but stopped answering pings for about a minute — deadlocked or stuck in a native call. Restart it. |
| `session.status().heartbeat` shows `misses > 0` (the playground's HEARTBEAT readout says "no reply") | The kernel process is still there but is not answering pings — it is stuck (deadlocked, or inside a native call that never returns), not merely busy: a busy kernel keeps answering from its heartbeat thread. If it does not recover, the heartbeat gives up after about a minute and you get the `'exit'` event above; restart it earlier if you can't wait. |
| `'exit'` with `kernel process exited unexpectedly` while the heartbeat looked healthy a moment before | The process died between pings; process exit is detected by the OS process handle within milliseconds and does not wait for the heartbeat. `heartbeat` is a liveness hint, the exit event is authoritative. |
| `'exit'` with `WebSocket connection to the supervisor closed unexpectedly` | The supervisor process is gone; create a new `SessionManager`. |
| `Cannot restart session <id>: it was already stopped` | `stop()` is final. Create a new session. |
| Orphaned `elara`/`carpo` processes after a hard kill of Node | Kernels normally die with the supervisor (a Windows job object kills them when it exits); a supervisor that itself survived (killed test runner) keeps them. End the `themisto` process. |

## Interrupt does nothing

- Idle kernel: nothing to interrupt (that is normal; `interrupt()` still returns `true`).
- Long native call (a C extension, a blocking read): interrupted only when it returns to the interpreter.
- Waiting on `input()`: answer it first.
- Linux/macOS: the interrupt path there (`pthread_kill`) is newer than the Windows one; if a Python `time.sleep()` does not wake on your platform, please report it with the platform and Python version.

## Playground

| Symptom | Fix |
|---|---|
| `next: not found` / dependencies missing | `npm run playground:install` (the playground has its own `node_modules`). |
| Sessions fail with the supervisor executable missing | Run `npm run build` first; or set `JOVIAN_NATIVE_DIR`. |
| R/Python not pre-filled | Set `R_HOME` / `PYTHONHOME`, or enter the paths in the *New Kernel Session* dialog. |
| Port in use | `PLAYGROUND_PORT=4200 npm run playground`. |
