# Jovian

[![CI](https://github.com/damurka/jovian/actions/workflows/ci.yml/badge.svg)](https://github.com/damurka/jovian/actions/workflows/ci.yml)

Jovian runs language kernels as supervised Jupyter kernels, embeddable in Node.js and Electron applications. It ships two kernels today — R and Python — selected per session via `kernelType`. The pieces are named after moons of Jupiter, mirroring how Positron splits Amalthea / Ark / Kallichore:

| Name | Role |
|---|---|
| **Jovian** | This npm package: a TypeScript client (`lib/`) over the native binaries below. |
| **Adrastea** | Language-neutral Jupyter kernel framework — protocol, ZMQ transport, kernel core (`native/`). |
| **Elara** | The R kernel: embeds R on top of Adrastea (`elara.exe` / `elara`). |
| **Carpo** | The Python kernel: embeds CPython on top of Adrastea the same way (`carpo.exe` / `carpo`). |
| **Themisto** | The kernel supervisor: spawns and monitors kernel processes, re-exposing sessions over HTTP + WebSocket (`themisto.exe` / `themisto`) — the role Kallichore plays for Ark. |
| [hera](packages/hera) | The R companion package loaded inside an Elara session. |

See [`docs/architecture/overview.md`](docs/architecture/overview.md) for the full component and directory breakdown, and [`docs/cpp-usage.md`](docs/cpp-usage.md) for using/extending the C++ side directly (e.g. writing a new language kernel on top of Adrastea).

## Usage

```typescript
import { SessionManager } from 'jovian';

const manager = new SessionManager();

// kernelType defaults to 'r'.
const rSession = await manager.createSession({ rHome: '/path/to/R' });
const rResult = await rSession.execute('x <- 1:10; mean(x)');
console.log(rResult.success, rResult.output);

const pySession = await manager.createSession({ kernelType: 'python', pythonHome: '/path/to/python' });
const pyResult = await pySession.execute('sum(range(1, 11))');
console.log(pyResult.success, pyResult.output);

await manager.stopAll();
```

Each session spawns its own `elara`/`carpo` process (per its `kernelType`) via `themisto`, so one session blocking on a long-running call (e.g. a Shiny app via `session.createShiny()`, or a long Python loop) never starves another. See [`examples/`](examples) for runnable examples: [`basic/simple-execute.js`](examples/basic/simple-execute.js) (a single session) and [`advanced/two-sessions.js`](examples/advanced/two-sessions.js) (two concurrent sessions, one blocking, one not).

## Building

Prerequisites: an R installation, [vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` set), and Node.js. A Python installation is only needed at *runtime* to actually run a Python session (and to build/run `CarpoTest`) — `carpo` itself loads Python's C API dynamically, so it compiles without one.

```sh
npm install
npm run build        # native (elara + carpo + themisto) + TypeScript
npm test              # native ctest + TypeScript unit + integration tests
```

Other useful scripts: `npm run dev` (TypeScript watch mode), `npm run playground` (a browser + terminal REPL for exercising a live session, with a kernel-type selector — see [`tools/playground`](tools/playground)), `npm run jupyter:kernelspec` (writes standard Jupyter `kernel.json` files so `elara`/`carpo` can be launched directly by `jupyter lab`/`jupyter console`, no supervisor involved; `-- --only=r`/`--only=python` to write just one).

CI builds and tests this repo on Windows, Linux and macOS on every push to `main` — see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Status

Both kernels support execute, streaming output (real-time for both -- Carpo via a native stdout/stderr callback, mirroring Elara's use of R's own `WriteConsoleEx`), display data, errors, completion, inspection, interrupt, and session restart/crash-recovery. Elara additionally has a standard Jupyter connection-file launch mode alongside the Themisto-supervised one (Carpo's kernelspec exists too, via `npm run jupyter:kernelspec`, but hasn't been exercised against a real `jupyter lab`/`jupyter console` install). Carpo also supports pointing a session at a venv's `site-packages`.
