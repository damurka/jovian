# Jovian Architecture Overview

## Introduction

Jovian runs language kernels as supervised Jupyter kernels, embeddable in Node.js and Electron applications. It ships a working R kernel today; a Python kernel ("Carpo") exists as build-time-opt-in scaffolding only -- see its own row below.

## Components and names

The parts are named after moons of Jupiter, mirroring how Positron splits Amalthea / Ark / Kallichore:

| Name | Role | Where |
|---|---|---|
| **Jovian** | The umbrella product and npm package (`jovian`): a TypeScript client (`lib/`) over the native binaries below | repo root |
| **Adrastea** | Language-neutral Jupyter kernel framework: protocol, ZMQ transport, kernel core, the abstract interpreter interface (`adrastea::`) -- built as its own static library, shared by Elara, Themisto, and Carpo | `native/src/adrastea`, `native/include/adrastea` |
| **Elara** | The R kernel: embeds R on top of Adrastea, loading R's shared library dynamically at runtime (`elara::`, the `elara` executable) | `native/src/elara` |
| **Themisto** | The kernel supervisor: spawns and monitors Elara processes, re-exposes sessions over HTTP + WebSocket (`themisto::`, the `themisto` executable) -- the role Kallichore plays for Ark | `native/src/themisto` |
| **Carpo** *(scaffolding only)* | A second `adrastea::Interpreter` on top of Adrastea, proving the extension point generalizes beyond R -- identifies itself correctly over the Jupyter protocol (`kernel_info_request`) but every request needing real Python execution replies with a structured "not implemented" error. Not built by default (`JOVIAN_BUILD_CARPO`, default `OFF`) | `native/src/carpo`, `native/include/carpo` |
| [hera](../../packages/hera) | The R companion package loaded inside an Elara session | `packages/hera` |

There is no Node-API addon and no in-process engine: `lib/session/` talks to Themisto over plain HTTP (session lifecycle) and WebSocket (execute/interrupt/message streaming), and Themisto spawns one Elara process per session. This is deliberate -- a session blocking on a long-running R call (e.g. a Shiny app) can never starve another session, since they're different OS processes with different embedded R interpreters entirely.

## High-Level Architecture

```
┌──────────────────────────────────────────────────────────┐
│              TypeScript Layer (lib/) -- "Jovian"          │
│   SessionManager / Session, MessageRouter, handlers,      │
│   ExecutionQueue, MiddlewareChain                         │
└───────────────────────┬────────────────────────────────────┘
                         │ HTTP (create/stop/restart) + WebSocket (execute/messages)
┌───────────────────────┴────────────────────────────────────┐
│           Supervisor process (themisto.exe) -- "Themisto"  │
│   SessionRegistry, KernelProcess (spawns/monitors),         │
│   HttpApi, WsRelay -- a ZMQ *client* to each kernel below   │
└───────────────────────┬────────────────────────────────────┘
                         │ ZMQ (one kernel process per session)
┌───────────────────────┴────────────────────────────────────┐
│      Kernel process (elara.exe, one per session) -- "Elara" │
│   Built on Adrastea (Kernel/KernelCore, ZMQ server,         │
│   Jupyter message handling) + RInterpreter on top           │
└───────────────────────┬────────────────────────────────────┘
                         │ R C API (R's shared library, loaded dynamically at runtime)
┌───────────────────────┴────────────────────────────────────┐
│                R Package (packages/hera/) -- "hera"         │
│         Execution │ Completion │ Inspection                 │
└──────────────────────────────────────────────────────────┘
```

## Directory Structure

### `native/` — C++ Native Code

Every folder under `native/src/` maps to exactly one of the four targets -- Adrastea, Elara, Themisto, or Carpo -- no mixing:

```
native/
├── include/
│   ├── adrastea/         # Adrastea's public API headers
│   ├── elara/            # Elara's public headers (engine.hpp, interpreter_r.hpp)
│   └── carpo/            # Carpo's public headers (engine.hpp, interpreter_py.hpp) -- scaffolding only
├── src/
│   ├── adrastea/         # The `adrastea` static library
│   │   ├── core/         # kernel/ (Kernel, KernelCore), execution/ (abstract Interpreter),
│   │   │                 # messaging/ (Jupyter messages), history/
│   │   ├── transport/    # server/ (ZMQ ROUTER/PUB), client/ (ZMQ DEALER/SUB), common/
│   │   ├── platform/     # OS-specific (guid, process helpers)
│   │   └── utils/        # logging, input, helpers
│   ├── elara/            # The `elara` executable
│   │   ├── r/            # R C-API interop: routine.cpp (.Call() routines),
│   │   │                 # r_dynlib.{hpp,cpp} (dynamic R loading), interpreter_r.cpp (RInterpreter)
│   │   ├── bridge/       # elara::Server -- env setup, boots the Kernel
│   │   └── elara.cpp     # main(): CLI args, registration handshake with Themisto
│   ├── themisto/         # The `themisto` executable
│   │   ├── session_registry.{hpp,cpp}  # session lifecycle, one KernelProcess per session
│   │   ├── kernel_process.{hpp,cpp}    # spawns/monitors one elara.exe
│   │   ├── http_api.{hpp,cpp}          # REST: create/list/get/delete/restart
│   │   └── ws_relay.{hpp,cpp}          # WebSocket: execute/interrupt/message streaming
│   └── carpo/            # The `carpo` executable -- SCAFFOLDING ONLY (JOVIAN_BUILD_CARPO, default OFF)
│       ├── interpreter_py.cpp  # PyInterpreter: kernel_info_request works, everything else
│       │                       # needing real execution replies "not implemented"
│       ├── bridge/       # carpo::Server -- mirrors elara/bridge/ exactly
│       └── carpo.cpp     # main() -- mirrors elara.cpp exactly
└── test/                 # C++ tests, mirroring src/'s per-feature split
    ├── adrastea/         # message, middleware, authentication, zmq_serializer,
    │                     # kernel_configuration, client_zmq/heartbeat/handshake
    ├── elara/            # elara.exe's own startup behavior (e.g. missing-R handling)
    ├── themisto/         # SessionRegistry/KernelProcess, against a real elara.exe
    └── carpo/            # PyInterpreter's stub behavior (only built when JOVIAN_BUILD_CARPO=ON)
```

`adrastea` is a static library (not a DLL) -- elara/themisto each link it directly, so both ship as standalone executables with no companion library to distribute. There is currently no installable/exported CMake package for it outside this repo's own build (`add_subdirectory(native)`); see [C++ usage](cpp-usage.md) for what consuming it from outside this repo actually looks like today.

### `lib/` — TypeScript Code (the `jovian` npm package)

```
lib/
├── session/          # SessionManager, Session, SupervisorClient
├── messaging/        # MessageRouter
├── handlers/         # Per-msg_type handlers (stream, execute_result, display_data, error)
├── execution/        # ExecutionQueue
├── middleware/        # Plugin system (logging, metrics)
├── utils/            # Logger, network helpers
├── types/            # Public type definitions
└── index.ts          # Package entry point
```

### `packages/` — R Packages

```
packages/
└── hera/                 # R kernel companion package, loaded inside every Elara session
    └── R/
        ├── execute.R     # Code execution
        ├── completion.R  # Code completion
        └── inspect.R     # Object inspection
```

### `examples/` and `tools/`

- `examples/basic/simple-execute.js` -- one session, one execute() call, minimal.
- `examples/advanced/two-sessions.js` -- two concurrent sessions, demonstrating that one session's blocking call never starves another.
- `tools/playground/` -- a browser + terminal REPL for exercising a live session (`npm run playground`).
- `tools/jupyter-kernelspec/` -- writes a standard Jupyter `kernel.json` so `elara` can be launched directly by `jupyter lab`/`jupyter console`, no supervisor involved (`npm run jupyter:kernelspec`).

## Component Details

### Adrastea: Kernel Core

- `Kernel` -- binds ZMQ sockets, runs the poll loop.
- `KernelCore` -- dispatches an incoming Jupyter message to the registered `Interpreter`, catches exceptions and turns them into an `execute_reply` with `status: error` rather than letting the kernel die silently.
- `Interpreter` -- abstract base class; `RInterpreter` (Elara) and `PyInterpreter` (Carpo, scaffolding) are its two concrete implementations. A global registry (`registerInterpreter`/`getInterpreter`) lets framework-level code (e.g. `input.cpp`'s blocking input request) reach "the" active interpreter without knowing which language it is.

### Elara: R Interpreter Integration

- `RInterpreter` -- embeds R (`Rf_initEmbeddedR`), runs on the process's own main thread (not a background thread -- R's C-stack-bounds auto-detection assumes that).
- Dynamic R loading (`native/src/elara/r/r_dynlib.{hpp,cpp}`) -- R's shared library (`R.dll` / `libR.so` / `libR.dylib`) is loaded at runtime via `LoadLibrary`/`dlopen`, not linked at build time, the same architecture Positron's Ark uses. This means: switching R installations is a runtime `R_HOME` decision needing no rebuild, and a missing/incompatible R surfaces as a clean, catchable error (exit code 1, an actionable message) instead of the OS refusing to start the process at all.
- Code execution itself is delegated to the bundled `hera` R package via `.Call()`.

### Carpo: Python Interpreter Scaffolding (not yet functional)

- `PyInterpreter` -- exists to prove `adrastea::Interpreter` genuinely generalizes beyond Elara/R, not to run Python. `kernelInfoRequestImpl()` is fully implemented (identifies as `carpo`/`python` over the Jupyter protocol); every other `*RequestImpl()` that would need real execution (`executeRequestImpl`, `completeRequestImpl`, ...) replies with a structured "not implemented" error instead of hanging, crashing, or pretending to work.
- Not built by default -- `cmake ... -DJOVIAN_BUILD_CARPO=ON` opts in.
- A real implementation would embed Python the way `RInterpreter` embeds R: most likely dynamically loading `libpython` at runtime (mirroring `native/src/elara/r/r_dynlib.hpp`) rather than linking a specific Python version at build time, for the same "switch versions without a rebuild, fail cleanly if missing" reasons. See [`docs/cpp-usage.md`](../cpp-usage.md)'s "Writing a new interpreter" section.

### Adrastea: ZMQ Transport Layer

- Implements the Jupyter wire protocol over ZMQ (shell, control, stdin, iopub, heartbeat channels).
- `ServerZmq` (kernel side) / `ClientZmq` (Themisto's side, talking to a kernel).
- `ZmqSerializer` -- message framing/(de)serialization; `Authentication` -- HMAC signing/verification of every message.

### Themisto: Supervisor

- `SessionRegistry` -- owns the map of live sessions, each with its own `KernelProcess` and `ClientZmq`. `createSession`/`restartSession` can each take a full set of R options (`rHome`/`rPath`/etc.), so restarting a session can switch R installations in place without creating a new session.
- `KernelProcess` -- spawns one `elara.exe`, pumps its stdout/stderr, tracks liveness.
- `HttpApi` -- REST surface for session lifecycle (create/list/get/delete/restart).
- `WsRelay` -- the WebSocket side: execute/interrupt requests in, streamed Jupyter messages out.

### Jovian: TypeScript Client

- `SessionManager` -- creates/tracks `Session`s, owns the shared `SupervisorClient` (spawns `themisto.exe` lazily, on first `createSession()`).
- `Session` -- one WebSocket connection to one Themisto-managed session; `MessageRouter` + per-`msg_type` handlers + `ExecutionQueue` (single-flight execute()/createShiny() calls, timeout handling) + `MiddlewareChain` (optional logging/metrics plugins).

## Message Flow

### Execute Request Flow

```
1. TypeScript: session.execute(code)
   -- ExecutionQueue enqueues it, sends { type: 'execute', id, code } over the WebSocket
2. Themisto: WsRelay receives the frame, calls SessionRegistry::sendExecute()
3. Themisto → Elara: ClientZmq sends an execute_request over the shell channel
4. Elara: KernelCore dispatches to RInterpreter::executeRequestImpl()
5. Elara → hera: hera:::hera_call("execute", code, ...) via R's .Call()
6. hera → R runtime: parses and evaluates the code
7. Results flow back: hera → RInterpreter → KernelCore → ZMQ (execute_reply, iopub stream/
   execute_result/error) → Themisto's ClientZmq → WsRelay → the WebSocket → Session
8. TypeScript: the execute() promise resolves; 'stream'/'message' events fire along the way
```

## Threading Model

- **Elara**: R and the kernel's ZMQ poll loop run on the *same* thread (the process's main thread) -- deliberately, since R's own C-stack-bounds auto-detection assumes it's running on the process's real main thread.
- **Themisto**: one thread for its HTTP listener, one per spawned kernel process (pumping its stdout/stderr), one per session (polling that session's ZMQ client for messages to relay over its WebSocket).

## Build System

### CMake (C++)

- Root `CMakeLists.txt` -- resolves dependencies (`find_package`), sets `dist/native/$<CONFIG>` as the output directory.
- `native/CMakeLists.txt` -- defines the `adrastea` static library plus the `elara`/`themisto` executables (`JOVIAN_BUILD_ELARA`/`JOVIAN_BUILD_THEMISTO` options).
- `native/test/CMakeLists.txt` -- the per-feature test executables (see Testing Strategy below).
- Dependencies: ZeroMQ, cppzmq, nlohmann_json, OpenSSL, R (headers only -- see Elara's dynamic R loading above), httplib + ixwebsocket (Themisto only).

### TypeScript

- `tsconfig.json` -- compiles `lib/` → `dist/lib/` (ES modules, type definitions generated alongside).

### Build outputs — one root: `dist/`

- `dist/lib/` -- compiled TypeScript.
- `dist/native/` -- the main CMake build (`elara.exe`, `themisto.exe`, `adrastea.lib`, runtime DLLs).
- `dist/native-test/` -- a separate CMake build tree with `JOVIAN_BUILD_TESTS=ON`, kept apart so test targets don't leak into the main build's cache.
- `dist/ide/<preset>` and `dist/ide-install/<preset>` -- produced only if you configure via `CMakePresets.json` from an IDE (Visual Studio / VS Code CMake Tools); the npm scripts below never write here.

```bash
npm run build    # native (elara + themisto) + TypeScript, into dist/
npm run clean    # remove dist/ (and any leftover legacy build/out/.cmake-js/ dirs)
npm run dev      # watch mode for TypeScript
```

## Testing Strategy

Tests are split the same way the source is: one native test executable/CTest entry per feature (Adrastea/Elara/Themisto/Carpo), plus Jovian's own TypeScript suite.

- `native/test/adrastea/` -- MessageTest, MiddlewareTest, AuthenticationTest, ZmqSerializerTest, KernelConfigurationTest, ClientZmqTest, ClientHeartbeatTest, ClientHandshakeZmqTest. No R, no spawned process.
- `native/test/elara/` -- ElaraTest: elara.exe's own startup behavior, spawned directly (e.g. the missing-R-installation failure path). Doesn't go through SessionRegistry.
- `native/test/themisto/` -- SessionRegistryTest (drives a real `elara.exe` through SessionRegistry directly: create/execute/restart/stop, concurrency races) and KernelProcessTest (process spawn/liveness/kill against a dummy helper process, no R needed).
- `native/test/carpo/` -- CarpoTest: `PyInterpreter`'s stub behavior (kernel_info_request identifies correctly, execute_request replies "not implemented"). Only built/registered when `JOVIAN_BUILD_CARPO=ON`.
- `test/unit/lib/` -- TypeScript unit tests (ExecutionQueue, MessageRouter, MiddlewareChain, Session, message parsing).
- `test/integration/` -- drives a real `SessionManager`/`Session` (and the `elara`/`themisto` processes behind them) end to end over HTTP/WebSocket.

```bash
npm test    # native ctest (all three feature suites) + TS unit + TS integration
```

## Deployment

### Package contents (`package.json`'s `files`)

```
dist/
├── lib/                     # compiled TypeScript (index.js, index.d.ts, ...)
└── native/Release/
    ├── elara.exe            # R kernel (one process per session)
    ├── themisto.exe         # supervisor (spawns and monitors kernels)
    ├── adrastea.lib         # (excluded from what actually ships -- build artifact only)
    └── *.dll                # runtime dependencies (ZeroMQ, OpenSSL, ...)
packages/hera/                # the R companion package, installed into a session's R library
```

Test binaries (`*_test.exe`, `dummy_process_helper.*`, `gtest*.dll`, `*.lib`, `*.pdb`) are explicitly excluded from what ships, via negated glob entries in `files`.

### Usage

```typescript
import { SessionManager } from 'jovian';

const manager = new SessionManager();
const session = await manager.createSession({ rHome: '/path/to/R' });

const result = await session.execute('x <- 1:10; mean(x)');
console.log(result.success, result.output);

await manager.stopAll();
```

See [`examples/`](../../examples) for fuller, runnable examples.

## Style Guide

- **C++**: Google C++ Style Guide.
- **TypeScript**: Standard + Prettier.
- **Formatting**: clang-format (C++), Prettier (TS) -- `npm run format`.
- **Linting**: clang-tidy (C++), ESLint (TS) -- `npm run lint`.

## References

- [Jupyter Kernel Protocol](https://jupyter-client.readthedocs.io/)
- [ZeroMQ Guide](https://zguide.zeromq.org/)
- [R Internals](https://cran.r-project.org/doc/manuals/r-release/R-ints.html)
- [Positron's Ark](https://github.com/posit-dev/positron) -- the architecture Jovian's Elara/Themisto split, and Elara's dynamic R loading, are both modeled on.
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
