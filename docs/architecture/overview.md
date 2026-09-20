# Jovian Architecture Overview

## Introduction

Jovian runs language kernels as supervised Jupyter kernels, embeddable in Node.js and Electron applications. It ships two working kernels today -- R ("Elara") and Python ("Carpo") -- selected per session via `EngineOptions.kernelType` (`'r'` | `'python'`, default `'r'`).

## Components and names

The parts are named after moons of Jupiter, mirroring how Positron splits Amalthea / Ark / Kallichore:

| Name | Role | Where |
|---|---|---|
| **Jovian** | The umbrella product and npm package (`jovian`): a TypeScript client (`lib/`) over the native binaries below | repo root |
| **Adrastea** | Language-neutral Jupyter kernel framework: protocol, ZMQ transport, kernel core, the abstract interpreter interface (`adrastea::`) -- built as its own static library, shared by Elara, Themisto, and Carpo | `native/src/adrastea`, `native/include/adrastea` |
| **Elara** | The R kernel: embeds R on top of Adrastea, loading R's shared library dynamically at runtime (`elara::`, the `elara` executable) | `native/src/elara` |
| **Carpo** | The Python kernel: embeds CPython on top of Adrastea the same way, loading Python's shared library dynamically at runtime (`carpo::`, the `carpo` executable). Built by default (`JOVIAN_BUILD_CARPO`, default `ON`) | `native/src/carpo`, `native/include/carpo` |
| **Themisto** | The kernel supervisor: spawns and monitors Elara/Carpo processes (one `kernelType` per session), re-exposes sessions over HTTP + WebSocket (`themisto::`, the `themisto` executable) -- the role Kallichore plays for Ark | `native/src/themisto` |
| [hera](../../packages/hera) | The R companion package loaded inside an Elara session | `packages/hera` |

There is no Node-API addon and no in-process engine: `lib/session/` talks to Themisto over plain HTTP (session lifecycle) and WebSocket (execute/interrupt/message streaming), and Themisto spawns one Elara or Carpo process per session, per that session's `kernelType`. This is deliberate -- a session blocking on a long-running call (e.g. a Shiny app, or a long Python loop) can never starve another session, since they're different OS processes with different embedded interpreters entirely.

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
│   SessionRegistry (keyed by kernelType: 'r' | 'python'),    │
│   KernelProcess (spawns/monitors), HttpApi, WsRelay --      │
│   a ZMQ *client* to each kernel below                       │
└──────────────┬──────────────────────────────┬───────────────┘
               │ ZMQ                          │ ZMQ
┌──────────────┴───────────────┐  ┌───────────┴───────────────┐
│ Kernel process (elara.exe)   │  │ Kernel process (carpo.exe) │
│ "Elara" -- Adrastea +        │  │ "Carpo" -- Adrastea +      │
│ RInterpreter                 │  │ PyInterpreter              │
└──────────────┬───────────────┘  └───────────┬────────────────┘
               │ R C API (dynamic)             │ Python C API (dynamic)
┌──────────────┴───────────────┐  ┌───────────┴────────────────┐
│ R Package (packages/hera/)   │  │ Inline Python bootstrap     │
│ "hera" -- Execution │        │  │ source (interpreter_py.cpp) │
│ Completion │ Inspection      │  │ -- same job as hera, small  │
│                               │  │ enough not to need its own  │
│                               │  │ installable package         │
└───────────────────────────────┘  └──────────────────────────────┘
```

## Directory Structure

### `native/` — C++ Native Code

Every folder under `native/src/` maps to exactly one of the four targets -- Adrastea, Elara, Themisto, or Carpo -- no mixing:

```
native/
├── include/
│   ├── adrastea/         # Adrastea's public API headers
│   ├── elara/            # Elara's public headers (engine.hpp, interpreter_r.hpp)
│   └── carpo/            # Carpo's public headers (engine.hpp, interpreter_py.hpp)
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
│   │   ├── session_registry.{hpp,cpp}  # session lifecycle, one KernelProcess per session,
│   │   │                               # keyed by kernelType -> kernel executable path
│   │   ├── kernel_process.{hpp,cpp}    # spawns/monitors one elara.exe or carpo.exe
│   │   ├── http_api.{hpp,cpp}          # REST: create/list/get/delete/restart
│   │   └── ws_relay.{hpp,cpp}          # WebSocket: execute/interrupt/message streaming
│   └── carpo/            # The `carpo` executable
│       ├── py/           # Python C-API interop: py_dynlib.{hpp,cpp} (dynamic Python loading,
│       │                 # mirrors elara/r/r_dynlib.hpp)
│       ├── interpreter_py.cpp  # PyInterpreter: real execute/is_complete/complete/inspect,
│       │                       # backed by an inline Python bootstrap source (hera's equivalent)
│       ├── bridge/       # carpo::Server -- mirrors elara/bridge/ exactly
│       └── carpo.cpp     # main() -- mirrors elara.cpp exactly
└── test/                 # C++ tests, mirroring src/'s per-feature split
    ├── adrastea/         # message, middleware, authentication, zmq_serializer,
    │                     # kernel_configuration, client_zmq/heartbeat/handshake
    ├── elara/            # elara.exe's own startup behavior (e.g. missing-R handling)
    ├── themisto/         # SessionRegistry/KernelProcess, against a real elara.exe
    └── carpo/            # PyInterpreter's real behavior against a real Python install
                          # (skips itself via GTEST_SKIP if none is found at configure time)
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
- `tools/playground/` -- a browser + terminal REPL for exercising a live session (`npm run playground`), with a kernel-type selector (R or Python) in its "New Kernel Session" dialog.
- `tools/jupyter-kernelspec/` -- writes standard Jupyter `kernel.json` files (one for `elara`, one for `carpo`) so either can be launched directly by `jupyter lab`/`jupyter console`, no supervisor involved (`npm run jupyter:kernelspec`; `--only=r`/`--only=python` to write just one).

## Component Details

### Adrastea: Kernel Core

- `Kernel` -- binds ZMQ sockets, runs the poll loop.
- `KernelCore` -- dispatches an incoming Jupyter message to the registered `Interpreter`, catches exceptions and turns them into an `execute_reply` with `status: error` rather than letting the kernel die silently.
- `Interpreter` -- abstract base class; `RInterpreter` (Elara) and `PyInterpreter` (Carpo) are its two concrete implementations. A global registry (`registerInterpreter`/`getInterpreter`) lets framework-level code (e.g. `input.cpp`'s blocking input request) reach "the" active interpreter without knowing which language it is.

### Elara: R Interpreter Integration

- `RInterpreter` -- embeds R (`Rf_initEmbeddedR`), runs on the process's own main thread (not a background thread -- R's C-stack-bounds auto-detection assumes that).
- Dynamic R loading (`native/src/elara/r/r_dynlib.{hpp,cpp}`) -- R's shared library (`R.dll` / `libR.so` / `libR.dylib`) is loaded at runtime via `LoadLibrary`/`dlopen`, not linked at build time, the same architecture Positron's Ark uses. This means: switching R installations is a runtime `R_HOME` decision needing no rebuild, and a missing/incompatible R surfaces as a clean, catchable error (exit code 1, an actionable message) instead of the OS refusing to start the process at all.
- Code execution itself is delegated to the bundled `hera` R package via `.Call()`.

### Carpo: Python Interpreter Integration

- `PyInterpreter` -- embeds CPython (`Py_Initialize`), proving `adrastea::Interpreter` genuinely generalizes beyond Elara/R. Real `executeRequestImpl`, `isCompleteRequestImpl`, `completeRequestImpl`, and `inspectRequestImpl` -- backed by a small inline Python bootstrap source (`interpreter_py.cpp`'s `kBootstrapSource`, hera's equivalent) using only the standard library (`ast`, `contextlib`, `traceback`, `codeop`, `rlcompleter`, `inspect`), exec'd once at construction into its own private namespace so none of it pollutes the user's `__main__`/`globals()`.
- Dynamic Python loading (`native/src/carpo/py/py_dynlib.{hpp,cpp}`) -- mirrors Elara's dynamic R loading exactly: Python's shared library (`pythonXY.dll` / `libpythonX.Y.so*` / `libpythonX.Y.dylib`) is loaded at runtime via `LoadLibrary`/`dlopen`, discovering the newest version present under `python_home` (unlike R.dll's fixed name, Python's shared library name embeds its version). Only the documented, stable-ABI subset of the C API is used -- `PyObject` stays fully opaque, reference counting goes through the real `Py_IncRef`/`Py_DecRef` functions rather than macros that would read/write `ob_refcnt` directly.
- Real-time stdout/stderr streaming -- a native C callback (`carpoNativeWriteStdout`/`WriteStderr`, wrapped into a Python-callable via `PyCFunction_NewEx`/`PyMethodDef`) is inserted directly into the bootstrap's globals before it runs; the bootstrap's `_CarpoStream` class calls it from `write()`, so every write publishes immediately -- the same granularity Elara gets for free from R's `WriteConsoleEx` callback.
- venv support -- `EnvironmentConfig::venv_path` reaches the bootstrap via a `CARPO_VENV_PATH` env var; the bootstrap prepends that venv's `site-packages` directory to `sys.path` on startup. `PYTHONHOME` still points at the base install either way -- an embedded interpreter needs the base install's actual libpython/stdlib regardless of which venv's packages should also be importable.
- Built by default (`JOVIAN_BUILD_CARPO`, default `ON`) -- like Elara's R dependency, Python is a *runtime* requirement (dynamically loaded), not a build-time one, so `carpo.exe` compiles fine even on a machine with no Python installed; it just can't run there without one.
- See [`docs/cpp-usage.md`](../cpp-usage.md)'s "Writing a new interpreter" section for the design in more depth.

### Adrastea: ZMQ Transport Layer

- Implements the Jupyter wire protocol over ZMQ (shell, control, stdin, iopub, heartbeat channels).
- `ServerZmq` (kernel side) / `ClientZmq` (Themisto's side, talking to a kernel).
- `ZmqSerializer` -- message framing/(de)serialization; `Authentication` -- HMAC signing/verification of every message.

### Themisto: Supervisor

- `SessionRegistry` -- owns the map of live sessions, each with its own `KernelProcess` and `ClientZmq`, plus a map of `kernelType` (`"r"`, `"python"`) to kernel executable path (`main.cpp` discovers both `elara`/`carpo` as siblings of `themisto` at startup). `createSession`/`restartSession` take a full set of options for either kernel type (`rHome`/`rPath`/`rLibs`/... or `pythonHome`/`pythonPath`/`venvPath`), so restarting a session can switch R/Python installations in place without creating a new session. A `createSession()` for a `kernelType` with no configured executable (e.g. `carpo` wasn't built) fails just that call with a clear error, not the whole supervisor.
- `KernelProcess` -- spawns one `elara.exe` or `carpo.exe` (whichever `kernelType` calls for), pumps its stdout/stderr (labeled by the spawned executable's own name), tracks liveness.
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
- **Carpo**: same shape -- Python and the kernel's ZMQ poll loop run on one thread. There is exactly one embedded Python interpreter per process, so the native stdout/stderr streaming callback (called synchronously from Python's own `write()` dispatch) needs no GIL handling beyond what's already implicit in that single-threaded embedding.
- **Themisto**: one thread for its HTTP listener, one per spawned kernel process (pumping its stdout/stderr), one per session (polling that session's ZMQ client for messages to relay over its WebSocket).

## Build System

### CMake (C++)

- Root `CMakeLists.txt` -- resolves dependencies (`find_package`), sets `dist/native/$<CONFIG>` as the output directory.
- `native/CMakeLists.txt` -- defines the `adrastea` static library plus the `elara`/`themisto`/`carpo` executables (`JOVIAN_BUILD_ELARA`/`JOVIAN_BUILD_THEMISTO`/`JOVIAN_BUILD_CARPO` options, all default `ON`).
- `native/test/CMakeLists.txt` -- the per-feature test executables (see Testing Strategy below). When `JOVIAN_BUILD_CARPO` is on, this also resolves a Python interpreter via CMake's own `find_package(Python3)` (not needed by `carpo` itself, only by `carpo_test` to know what to embed) and bakes its `sys.prefix` in as `CARPO_TEST_PYTHON_HOME`.
- Dependencies: ZeroMQ, cppzmq, nlohmann_json, OpenSSL, R (headers only -- see Elara's dynamic R loading above), httplib + ixwebsocket (Themisto only). Carpo needs no Python-specific CMake dependency at all -- its C API is loaded dynamically at runtime, not linked.

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
- `native/test/themisto/` -- SessionRegistryTest (drives a real `elara.exe` through SessionRegistry directly: create/execute/restart/stop, concurrency races, plus a kernelType-with-no-registered-executable failure path) and KernelProcessTest (process spawn/liveness/kill against a dummy helper process, no R needed).
- `native/test/carpo/` -- CarpoTest: `PyInterpreter` embedding a real Python interpreter -- execution (arithmetic, exceptions, persistence across calls, real-time stdout/stderr streaming), is-complete/complete/inspect against live objects, and an end-to-end venv activation test that actually creates a throwaway venv. Only built/registered when `JOVIAN_BUILD_CARPO=ON` (default), and skips itself (`GTEST_SKIP`) if no Python interpreter was found at configure time.
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
    ├── carpo.exe            # Python kernel (one process per session)
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

// kernelType defaults to 'r' -- every caller written before this option
// existed keeps working unchanged.
const rSession = await manager.createSession({ rHome: '/path/to/R' });
const rResult = await rSession.execute('x <- 1:10; mean(x)');
console.log(rResult.success, rResult.output);

const pySession = await manager.createSession({ kernelType: 'python', pythonHome: '/path/to/python' });
const pyResult = await pySession.execute('sum(range(1, 11))');
console.log(pyResult.success, pyResult.output);

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
