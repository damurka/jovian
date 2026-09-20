# Jovian Architecture Overview

Jovian runs language kernels as supervised Jupyter kernels that a Node.js or Electron process can drive. It ships two kernels — R (**Elara**) and Python (**Carpo**) — selected per session with `EngineOptions.kernelType` (`'r'` | `'python'`, default `'r'`).

## Components

The parts are named after moons of Jupiter:

| Name | Role | Where |
|---|---|---|
| **Jovian** | The umbrella product and npm package (`jovian`): a TypeScript client over the native binaries below | `lib/` |
| **Adrastea** | Language-neutral Jupyter kernel framework: wire protocol, ZMQ transport, kernel request loop, the abstract interpreter interface (`adrastea::`). Built as a **static library** shared by Elara, Carpo and Themisto | `native/src/adrastea`, `native/include/adrastea` |
| **Elara** | The R kernel: embeds R on top of Adrastea, loading R's shared library at runtime (`elara::`, the `elara` executable) | `native/src/elara` |
| **Carpo** | The Python kernel: embeds CPython on top of Adrastea the same way (`carpo::`, the `carpo` executable) | `native/src/carpo` |
| **Themisto** | The kernel supervisor: spawns and monitors one kernel process per session, speaks ZMQ to each, and re-exposes sessions over HTTP + WebSocket (`themisto::`, the `themisto` executable) | `native/src/themisto` |
| [hera](../../packages/hera) | The R companion package loaded inside every Elara session | `packages/hera` |

There is no Node-API addon and no in-process engine. `lib/` talks to Themisto over plain HTTP (session lifecycle) and WebSocket (execute, requests, message streaming), and Themisto spawns one Elara or Carpo process per session. That is deliberate: a session blocked in a long call (a Shiny app, a long Python loop) cannot starve another, because they are different processes with different interpreters, and only Themisto ever links a native ZMQ binding — the process that embeds `lib/` (an Electron main process, a VS Code extension host) needs no native dependency at all.

## Process model

```
┌─────────────────────────────────────────────────────────────┐
│  Your Node.js / Electron process                            │
│  lib/  SessionManager ─ Session ─ ExecutionQueue ─ Comm     │
└───────────────┬──────────────────────────┬──────────────────┘
      HTTP (create / stop / restart)   WebSocket, one per Session
                │                          │        (127.0.0.1 only)
┌───────────────┴──────────────────────────┴──────────────────┐
│  themisto  (one per SessionManager, spawned on first session)│
│  HttpApi · WsRelay · SessionRegistry                        │
│  per session: KernelProcess, ClientZmq, poll thread         │
└───────┬─────────────────────────────────────────┬───────────┘
        │ ZMQ: shell, control, stdin,             │ ZMQ (same, other session)
        │      iopub, heartbeat (127.0.0.1)       │
┌───────┴─────────────────┐              ┌────────┴─────────────────┐
│ elara  (R session)      │              │ carpo  (Python session)  │
│ Adrastea + RInterpreter │              │ Adrastea + PyInterpreter │
└───────┬─────────────────┘              └────────┬─────────────────┘
        │ R C API (dlopen / LoadLibrary)          │ Python C API (dynamic)
   R + packages/hera                         CPython + bootstrap module
```

- **Session = process.** `POST /sessions` makes Themisto spawn `elara` or `carpo` (whichever `kernelType` names), wait for it to register, and connect a ZMQ client to it.
- **Registration handshake.** Themisto binds one registration `ROUTER` socket and passes its address and a shared HMAC key on the kernel's command line (`--registration-ip`, `--registration-port`, `--key`). The kernel binds its five sockets on free ports and sends the port numbers to that registration socket (signed with the key). Themisto waits up to 60 s, aborting early if the kernel process dies first.
- **Authentication.** Every ZMQ message is signed (`hmac-sha256`) with that key; the key is shared by all kernels one Themisto spawns.
- **Loopback only.** Themisto's HTTP and WebSocket servers bind `127.0.0.1` on OS-assigned ports; kernel ports are `127.0.0.1` too.
- **Lifetime.** On Windows every kernel is assigned to a job object with `KILL_ON_JOB_CLOSE`, so kernels die with Themisto however Themisto exits. `SupervisorClient.kill()` (called by `SessionManager.stopAll()/killAll()` and an `exit` handler) ends Themisto.

## Channels and threads

### Jupyter channels

| Channel | ZMQ pattern (kernel side) | Used for |
|---|---|---|
| shell | ROUTER | `execute`, `complete`, `inspect`, `is_complete`, `kernel_info`, `history`, `comm_*` requests and their replies |
| control | ROUTER | `interrupt_request`, `shutdown_request` and their replies |
| stdin | ROUTER | `input_request` from the kernel, `input_reply` from the client |
| iopub | XPUB, owned by a publisher thread (the main thread hands it messages over an in-process PUB) | `status`, `execute_input`, `stream`, `display_data`, `update_display_data`, `clear_output`, `execute_result`, `error`, kernel-initiated `comm_*` |
| heartbeat | ROUTER (the supervisor's client uses REQ) | liveness pings |

Themisto's client side uses DEALER sockets for shell/control/stdin, a SUB for iopub and a REQ for the heartbeat. The three DEALERs share one explicit ZMQ routing id: the kernel addresses an `input_request` on the stdin ROUTER using the identity it captured from the *shell* message that started the execution, which only reaches the client if its stdin socket presents the same identity.

### Inside a kernel process (Elara / Carpo)

| Thread | Job |
|---|---|
| **Main thread** | Runs the ZMQ poll loop (shell + control) **and** the interpreter: R or Python executes on the process's own main thread (R's C-stack-bounds detection assumes it; Python's `KeyboardInterrupt` is raised on the thread that initialised it). |
| **Publisher** | Owns the iopub XPUB socket; the main thread hands it messages. |
| **Heartbeat** | Answers ping/pong, on its own thread — so it keeps answering while the main thread is busy running code. |
| **Control watcher** | Started lazily; alive for the kernel's lifetime but only *active* while code is executing (see below). |

Because the interpreter runs on the same thread that reads sockets, a control message sent mid-execution would normally sit unread until the execution finished — useless for interrupt. The **control watcher** fixes that:

- `KernelCore::executeRequest` brackets the interpreter call with `Server::beginExecution()` / `endExecution()`.
- Between those calls, the watcher thread polls the control socket (every 5 ms). An `interrupt_request` is dispatched immediately, on the watcher thread: the interpreter's `interruptRequestImpl()` flags the interpreter (R: sets `R_interrupts_pending` on POSIX / `UserBreak` on Windows; Python: delivers a real SIGINT — `raise(SIGINT)` on Windows, `pthread_kill` of the interpreter thread on POSIX — after the bootstrap installed `signal.default_int_handler`), and replies on the control channel. Any *other* control message (a `shutdown_request`, say) is queued and delivered by the main loop after the execution ends, in order, exactly as before.
- Interrupting an idle kernel is a no-op — the flag is only set while an execution is actually running, so a stale break can never abort the *next* execution.
- Locking: the control `ROUTER` is guarded by a recursive mutex (the interrupt handler, running on the watcher thread, replies through the same send path); iopub publishing is guarded by a mutex (the watcher publishes `status`/`interrupt` messages while the main thread streams output through the same publishing socket).
- **Stream flusher.** `Interpreter::publishStream` coalesces stdout/stderr text (one message per ~50 ms or 16 KB) and lazily starts a small flusher thread that publishes stale buffered text while the interpreter is busy computing and not writing; everything else that is published (results, display data, errors, input prompts, the `execute_reply`) flushes the buffer first, so ordering is unchanged. Publishing from that thread goes through the same iopub mutex.

`interruptRequestImpl()` therefore runs on a thread other than the interpreter's. Implementations must only do thread-safe things (see [cpp-usage.md](../cpp-usage.md)).

### Inside Themisto

| Thread | Job |
|---|---|
| HTTP server | cpp-httplib listener (plus its worker threads) handling session create/list/get/delete/restart |
| WebSocket server | ixwebsocket server threads, one connection per `Session` object on the client |
| **Poll thread, one per session** | Every ~5 ms: checks whether the kernel OS process is still alive; drains the client's iopub queue and the shell, control and stdin channels; relays each message as a JSON frame to the attached WebSocket |
| Client iopub / heartbeat threads | Inside each `ClientZmq`: receive iopub into a queue; run the heartbeat — a ping roughly every 100 ms, up to 20 s to wait for each answer, 3 retries. Every answer updates a `HeartbeatStatus` (round trip, age of the last pong, consecutive misses) that `sessionToJson()` publishes as the session's `heartbeat` |
| Kernel output pump, one per kernel | Reads the kernel's stdout/stderr pipe and re-prints it on Themisto's stderr with an `[elara]` / `[carpo]` prefix |

Every operation targeting a session id (`sendExecute`, `sendRequest`, `stopSession`, `restartSession`, …) takes a per-id recursive mutex so a request that arrives mid-restart waits and then addresses whichever kernel is live afterwards. `DealerChannel` additionally serialises each DEALER socket (ZMQ sockets are not thread-safe, and the poll thread receives while HTTP/WS threads send); a "blocking" receive is a loop of short non-blocking attempts so it never starves a sender.

### Crash detection

Two independent mechanisms, fastest first:

1. **OS liveness** — the poll thread checks the kernel's process handle every iteration (~5 ms). A kernel killed from outside (Task Manager, a segfault, `os._exit()`) is reported as `kernelExit` almost at once, with the exit code decoded when recognised (e.g. `STATUS_ACCESS_VIOLATION`). The final messages the kernel published are drained first.
2. **Heartbeat** — for a kernel that is alive but stuck (deadlocked, in a native call): 3 missed pings at 20 s each ≈ 60–80 s, reported as `kernelExit` with a `heartbeat gave up waiting for a response` reason. The same channel doubles as a health readout: every answered ping records its round trip, exposed through `GET /sessions/:id` and `Session.status()`. Because the kernel replies from a dedicated thread, a kernel that is merely *busy* (a long cell) still answers — the heartbeat separates "busy" from "stuck".

A session whose stop/restart was *requested* sets an "expecting exit" flag first, so the orderly exit that follows is not reported as a crash.

## Dynamic loading of R and Python

Neither interpreter is linked at build time.

- **R** (`native/src/elara/r/r_dynlib.{hpp,cpp}`): `R.dll` / `libR.so` / `libR.dylib` is loaded with `LoadLibrary` / `dlopen` when the kernel starts (after `R_HOME` and `PATH` are set up). Switching R installations is a runtime `rHome` decision needing no rebuild, and a missing R surfaces as a clean, catchable error inside `elara` (exit code 1, an actionable message) instead of the OS refusing to start the process. On Windows, `R.dll` exports no hookable console pointers, so `RInterpreter` starts R with the documented embedding sequence (`R_DefParamsEx`, `R_SetParams`, `setup_Rmainloop`, …) and its own `ReadConsole` callback; older R (< 4.2) falls back to `Rf_initEmbeddedR`.
- **Python** (`native/src/carpo/py/py_dynlib.{hpp,cpp}`): the shared library is found by scanning `pythonHome` for the newest `python3NN.dll` / `libpython3.*.so*` / `libpython3.*.dylib` (the name embeds the version, unlike R's). Only the documented stable-ABI subset of the C API is used and `PyObject` stays opaque; reference counting goes through the real `Py_IncRef` / `Py_DecRef` functions.
- **POSIX library path.** A kernel's own interpreter library is `dlopen`ed by full path, but R's base packages and Python's extension modules have the shared library as an unqualified dependency. Themisto therefore sets `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` (`<R_HOME>/lib`, `<PYTHONHOME>/lib`) in the child *between `fork()` and `exec()`* — setting it from inside the running kernel would be too late, since the dynamic linker reads it at process start.

## Message flows

### Execute

```
Session.execute(code)
  └ ExecutionQueue (single-flight, timeout)         { type:"execute", id, code, options }
      └ WebSocket ─────────────────────────────────────────────────────────►  WsRelay
                                                                    SessionRegistry::sendExecute
                                                                     └ shell: execute_request ─► kernel
kernel main thread: KernelCore::executeRequest
   status busy (iopub) · execute_input · [beginExecution: control watcher on]
   RInterpreter → hera:::hera_call("execute", …)   |   PyInterpreter → __carpo_run(code, globals)
      stream (coalesced ≤ every ~50 ms / 16 KB) / display_data / execute_result / error   (iopub)
   [endExecution]  execute_reply (shell)  ·  status idle (iopub)
poll thread ── relays every message as {type:"message", channel, msg_type, parent_msg_id, content}
Session ── MessageRouter emits events; ExecutionQueue collects output for this id;
           execute_reply resolves the promise (ExecutionResult)
```

### Request / reply (`complete`, `inspect`, `is_complete`, `kernel_info`, `history`, `comm_info`)

```
Session.complete(code, pos)
  └ request(): id = uuid; pending[id] = {replyType:"complete_reply", timer}
      └ WS {type:"request", id, channel:"shell", msgType:"complete_request", content}
          └ WsRelay → SessionRegistry::sendRequest (whitelist!) → shell: complete_request ─► kernel
kernel (between executions, on the main thread) → complete_reply (shell)
poll thread → {type:"message", channel:"shell", msg_type:"complete_reply", parent_msg_id:id, …}
Session.settleRequest → resolves with the reply `content`; status "error"/"aborted" rejects
If the supervisor refuses: {type:"requestError", id, error} → rejects at once
```

Requests are answered on the kernel's main thread, so they wait for a running execution to finish. Only `interrupt_request` (control) is serviced during one.

### Interactive input

```
kernel: R readline() / Python input()  ──►  adrastea::blockingInputRequest()
   (only if the execute_request had allow_stdin; otherwise fail fast — Python raises
    RuntimeError, R reports on stderr and reads no input)
   kernel main thread sends input_request on the stdin ROUTER and BLOCKS on the reply
poll thread relays it: {type:"message", channel:"stdin", msg_type:"input_request", …}
Session emits 'input_request' {prompt, password}; the execution's timeout is cleared
caller: session.sendInputReply(value) → WS {type:"inputReply", value}
   → SessionRegistry::sendInputReply → stdin: input_reply → the kernel unblocks
```

### Interrupt

```
Session.interrupt() → request('interrupt_request') on channel "control"
   → control DEALER → kernel control ROUTER
kernel control-watcher thread (active only while code runs):
   interrupt_request → interpreter flags R / raises SIGINT for Python
   status busy/idle (iopub, parented to the interrupt) · interrupt_reply (control)
interpreter thread: R notices the flag at its next R_CheckUserInterrupt() /
   Python's handler raises KeyboardInterrupt → the execution ends with an error;
   execute_reply arrives as usual, the kernel stays alive
poll thread relays interrupt_reply (channel "control"); interrupt() resolves true
```

### Stop and restart

```
Session.stop()                                   Session.restart(options?)
  DELETE /sessions/:id                             POST /sessions/:id/restart   (optional new options)
     └ SessionRegistry::stopSession(id, false)        └ restartSession → stopSession(id, true) → spawn new kernel
                                                          under the SAME session id
stopSession:
  1. expectingExit = true                             (an orderly exit is not a crash)
  2. control: shutdown_request {restart}              (if the process is alive)
  3. poll thread keeps running ≤ 2 s and relays iopub "shutdown" + control shutdown_reply
  4. stop poll thread, tear down the ZMQ client, force-kill if the process is still alive
     (normal for a session blocked inside shiny::runApp())
  5. status = stopped; the session is removed from the registry
Session emits 'shutdown_reply' ({status, restart}); stop() then emits 'stopped',
restart() reconnects its WebSocket to the same URL and emits 'restarted'.
```

## Repository layout

```
native/
├── include/{adrastea,elara,carpo}/   public headers
├── src/
│   ├── adrastea/   core/ (kernel, execution, messaging, history) · transport/ (server, client, common)
│   │               · platform/ · utils/
│   ├── elara/      r/ (routine.cpp, r_dynlib, interpreter_r.cpp) · bridge/ · elara.cpp
│   ├── carpo/      py/ (py_dynlib) · interpreter_py.cpp · bridge/ · carpo.cpp
│   └── themisto/   main.cpp · session_registry · kernel_process · http_api · ws_relay
└── test/           adrastea/ · elara/ · carpo/ · themisto/   (GoogleTest, one CTest entry per feature)
lib/                session/ (SessionManager, Session, Comm, SupervisorClient) · messaging/ · handlers/
                    · execution/ (ExecutionQueue) · middleware/ · utils/ · types/
packages/hera/      R companion package
test/               unit/lib (TypeScript, no processes) · integration (real themisto + kernels)
tools/              playground/ (Next.js) · jupyter-kernelspec/
examples/           basic/ · advanced/
scripts/            build.js · test.js · clean.js · dev.js · format.js · coverage.js
docs/               this documentation
```

`adrastea` is a static library, so `elara`, `carpo` and `themisto` are standalone executables with no companion library to ship. There is no installable CMake package for it; see [C++ usage](../cpp-usage.md).

## Further reading

[Protocol](../protocol.md) · [Kernels](../kernels.md) · [API reference](../api/README.md) · [Development](../development.md) · [Jupyter messaging protocol](https://jupyter-client.readthedocs.io/en/stable/messaging.html) · [ZeroMQ Guide](https://zguide.zeromq.org/)
