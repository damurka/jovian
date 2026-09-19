# Jovian

[![CI](https://github.com/damurka/jovian/actions/workflows/ci.yml/badge.svg)](https://github.com/damurka/jovian/actions/workflows/ci.yml)

Jovian runs language kernels as supervised Jupyter kernels, embeddable in Node.js and Electron applications. It ships an R kernel today, with a Python kernel planned. The pieces are named after moons of Jupiter, mirroring how Positron splits Amalthea / Ark / Kallichore:

| Name | Role |
|---|---|
| **Jovian** | This npm package: a TypeScript client (`lib/`) over the native binaries below. |
| **Adrastea** | Language-neutral Jupyter kernel framework — protocol, ZMQ transport, kernel core (`native/`). |
| **Elara** | The R kernel: embeds R on top of Adrastea (`elara.exe` / `elara`). |
| **Themisto** | The kernel supervisor: spawns and monitors kernel processes, re-exposing sessions over HTTP + WebSocket (`themisto.exe` / `themisto`) — the role Kallichore plays for Ark. |
| [hera](packages/hera) | The R companion package loaded inside an Elara session. |

See [`docs/architecture/overview.md`](docs/architecture/overview.md) for the full component and directory breakdown.

## Usage

```typescript
import { SessionManager } from 'jovian';

const manager = new SessionManager();
const session = await manager.createSession({ rHome: '/path/to/R' });

const result = await session.execute('x <- 1:10; mean(x)');
console.log(result.success, result.output);

await manager.stopAll();
```

Each session spawns its own `elara` process via `themisto`, so one session blocking on a long-running call (e.g. a Shiny app via `session.createShiny()`) never starves another. See [`index.js`](index.js) for a fuller example with two concurrent sessions.

## Building

Prerequisites: an R installation, [vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` set), and Node.js.

```sh
npm install
npm run build        # native (elara + themisto) + TypeScript
npm test              # native ctest + TypeScript unit + integration tests
```

Other useful scripts: `npm run dev` (TypeScript watch mode), `npm run playground` (a browser + terminal REPL for exercising a live session — see [`tools/playground`](tools/playground)), `npm run jupyter:kernelspec` (writes a standard Jupyter `kernel.json` so `elara` can be launched directly by `jupyter lab`/`jupyter console`, no supervisor involved).

CI builds and tests this repo on Windows, Linux and macOS on every push to `main` — see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Status

Elara (R) is functional: execute, streaming output, display data, errors, completion, inspection, interrupt, session restart/crash-recovery, and a standard Jupyter connection-file launch mode alongside the Themisto-supervised one. A Python kernel (Carpo) is planned but not started.
