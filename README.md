# Jovian

[![CI](https://github.com/damurka/jovian/actions/workflows/ci.yml/badge.svg)](https://github.com/damurka/jovian/actions/workflows/ci.yml)

Jovian runs **R** and **Python** as supervised Jupyter kernels that you can drive from Node.js and Electron. Every session is its own operating-system process with its own embedded interpreter, so one session blocking on a long call (a Shiny app, a slow loop) never starves another, and a kernel that crashes takes only its own session with it.

```typescript
import { SessionManager } from '@damurka/jovian';

const manager = new SessionManager();

const r = await manager.createSession({ kernelType: 'r', workingDirectory: '/projects/analysis' });
const result = await r.execute('x <- 1:10; mean(x)');
console.log(result.success, result.output);

const py = await manager.createSession({ kernelType: 'python' });
console.log((await py.execute('sum(range(1, 11))')).success);

await manager.stopAll();
```

The pieces are named after moons of Jupiter:

| Name | Role |
|---|---|
| **Jovian** | This npm package: a TypeScript client (`lib/`) over the native binaries below. |
| **Adrastea** | Language-neutral Jupyter kernel framework — wire protocol, ZMQ transport, request loop, the abstract `Interpreter` interface (`native/`, a static library). |
| **Elara** | The R kernel: embeds R on top of Adrastea (`elara` / `elara.exe`). It includes [hera](packages/hera), the R package loaded in every R session (execution, completion, inspection, comms), which is installed into R for you. |
| **Carpo** | The Python kernel: embeds CPython on top of Adrastea the same way (`carpo` / `carpo.exe`). |
| **Themisto** | The kernel supervisor: spawns and monitors one kernel process per session and re-exposes sessions over HTTP + WebSocket (`themisto` / `themisto.exe`). |

## Install

```bash
npm install @damurka/jovian
```

The package ships **prebuilt** `themisto`, `elara` and `carpo` binaries — no compiler, CMake or vcpkg — for **Windows x64**, **Linux x64 and arm64**, and **macOS x64 (Intel) and arm64 (Apple Silicon)**. Windows on ARM and 32-bit ARM Linux are not supported (there is no ARM64 R for Windows yet); [build from source](docs/building.md#using-your-own-build) there. npm installs the matching `@damurka/jovian-<os>-<cpu>` package automatically as an optional dependency, so do not install with `--omit=optional` / `--no-optional`. Node.js ≥ 22.13 is required. The package is an ES module: use `import` (in a project with `"type": "module"`, or `.mjs`/`.mts` files), or `require()` it from CommonJS on Node 22.13+.

What you must already have on the machine:

- **R** (4.2 or newer; a build with a shared library, which the CRAN/Posit binaries and distribution packages are) for R sessions. If `rHome` is not passed, it is found from `$R_HOME`, then `R RHOME` (R on `PATH`), then the Windows registry; pass `rHome` to choose a specific installation. You do not need to install any R packages yourself. The `hera` R package every R session needs ships inside the npm package, and before the **first** R session the library installs it, together with its CRAN dependencies (`cli`, `evaluate`, `glue`, `IRdisplay`, `jsonlite`, `R6`, `repr`, `rlang` and what they need), into your R library. Nothing else (not even `remotes`) has to be installed first. This needs an internet connection, takes about 20 seconds where CRAN has binaries (Windows, macOS) and a few minutes on Linux, where packages are compiled from source and need a compiler (Ubuntu: `sudo apt install build-essential`). It happens once; later sessions start straight away. A package that is installed but cannot be loaded (Debian/Ubuntu `r-cran-*` packages built for an older R fail with `undefined symbol: SETLENGTH`) is reinstalled from CRAN into your own library. If it cannot finish, `createSession()` rejects with R's own reason. Set `JOVIAN_SKIP_R_SETUP=1` to skip this step and manage the packages yourself.

- **Python 3** with its shared library (optional, for Python sessions); if `pythonHome` is not passed, it is found from `$PYTHONHOME`, then the first `python3` / `python` on `PATH` (its `sys.base_prefix`); pass `pythonHome` to choose one.
- **Linux:** `libuuid` (`libuuid1`, present on nearly every system) and a glibc at least as new as the one the binaries were built against (Ubuntu 24.04's, 2.39). On an older distribution, [build from source](docs/building.md).
- **macOS:** 14 or newer.
- **Windows:** the [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist) (x64, 2015–2022) — the binaries use the dynamic C++ runtime; most machines already have it.

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

## Environment variables

| Variable | Meaning |
|---|---|
| `R_HOME` | R installation to use when `rHome` is not passed; otherwise the library asks `R RHOME`. |
| `PYTHONHOME` | Python installation prefix when `pythonHome` is not passed; otherwise the library asks `python3` / `python` for its `sys.base_prefix`. |
| `JOVIAN_NATIVE_DIR` | Directory holding `themisto`, `elara` and `carpo`. Default: the installed `@damurka/jovian-<os>-<cpu>` package. Use it to run your own build ([building from source](docs/building.md#using-your-own-build)). |
| `JOVIAN_LOG_LEVEL` | How much the library prints: `trace`, `debug`, `info`, `notice` (the default), `warn`, `error` or `silent`. `debug` also shows the kernels' start-up output. See [the API reference](docs/api/README.md#sessionmanager). |
| `JOVIAN_KERNEL_OUTPUT` | Set to print the kernels' own `[elara]` / `[carpo]` start-up output. |
| `JOVIAN_SKIP_R_SETUP` | Set to skip the one-time install of `hera` and its R packages. |

## Building from source

You only need this to work on Jovian or to run it on a platform without a prebuilt package: [docs/building.md](docs/building.md) has the requirements per platform (Visual Studio on Windows, a compiler and vcpkg elsewhere), the build and test commands, and how to point the library at your own build.

```sh
npm install --legacy-peer-deps
npm run build   # native kernels into dist/native, then TypeScript into dist/lib
npm test
```

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
| [Getting started](docs/getting-started.md) | Building from source → your first R and Python session. |
| [Architecture](docs/architecture/overview.md) | Components, process and thread model, message flows. |
| [Protocol](docs/protocol.md) | Jupyter messages supported, Themisto's HTTP API and WebSocket frames. |
| [API reference](docs/api/README.md) | `SessionManager`, `Session`, `Comm`, options and result types. |
| Guides | [Interactive input](docs/guides/interactive-input.md) · [Interrupting](docs/guides/interrupting.md) · [Comms](docs/guides/comms.md) · [History](docs/guides/history.md) · [Session lifecycle](docs/guides/sessions-lifecycle.md) · [R and Python environments](docs/guides/environments.md) · [Playground](docs/guides/playground.md) |
| [Kernels](docs/kernels.md) | How Elara and Carpo work, and where they differ. |
| [Building from source](docs/building.md) | Requirements per platform, build and test commands, using your own build. |
| [Development](docs/development.md) | Repo layout, build system, test layers, CI, debugging. |
| [Releasing](docs/releasing.md) | How the npm packages are built and published. |
| [Troubleshooting](docs/troubleshooting.md) | Real error messages and what causes them. |
| [C++ usage](docs/cpp-usage.md) | Using Adrastea from C++; writing a new language kernel. |

Runnable examples are in [`examples/`](examples): [`basic/simple-execute.js`](examples/basic/simple-execute.js) (one session) and [`advanced/two-sessions.js`](examples/advanced/two-sessions.js) (two concurrent sessions, one blocking), and [`interactive/ask-for-input.ts`](examples/interactive/ask-for-input.ts) (R's `readline()` and Python's `input()` answered at the terminal; see [the input guide](docs/guides/interactive-input.md)).

## Contributing

Issues and pull requests are welcome. [CONTRIBUTING.md](CONTRIBUTING.md) has the rules, and the [issue forms](https://github.com/damurka/jovian/issues/new/choose) ask for what is needed to reproduce a bug.
