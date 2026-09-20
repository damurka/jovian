# `Session` and `Comm`

A `Session` is one kernel process (R or Python) plus its WebSocket connection to the supervisor. You get one from [`SessionManager.createSession()`](README.md#sessionmanager); the constructor is not part of the public API.

```typescript
const session = await manager.createSession({ kernelType: 'python', pythonHome, workingDirectory: '/projects/a' });
session.on('error', () => {});                         // required, see README#events
session.on('stdout', (t) => process.stdout.write(t));
const result = await session.execute('print("hi"); 1 + 1');
```

## Properties

| Property | Type | Description |
|---|---|---|
| `info` | `{ sessionId, httpBase, wsBase }` | Where this session lives on the supervisor. `httpBase` + `/sessions/<sessionId>` is the supervisor's own view of it — `status()` reads exactly that. |
| `options` | `EngineOptions` | What the session is running with: the creation options, updated by any `restart(options)` that changed them. |
| `executionState` | `'busy'` \| `'idle'` \| `'starting'` \| `undefined` | The kernel's latest iopub `status`. Tracked per request, so an interrupt handled *during* an execution (its own busy/idle pair) does not flip the state to idle while the execution is still running. `undefined` until the first status. |

## Running code

### `execute(code, options?): Promise<ExecutionResult>`

Runs `code`. Calls are queued and sent to the kernel **one at a time** (a kernel runs one thing at a time anyway); each resolves when the kernel's `execute_reply` arrives. See [`ExecutionOptions` / `ExecutionResult`](types.md#execution). Notable options:

- `timeout` — ms, default 30 000, `0` for none. When it fires, `execute()` rejects with `Execution timed out after <n>ms (the kernel was interrupted)` **and the kernel is interrupted** (so it does not keep running code nobody is waiting for, which would block everything queued behind it).
- `interruptOnTimeout: false` — on a timeout, only reject: leave the kernel running (the message then ends after `ms`, without the parenthesis). The default is `true`.
- `allowStdin: true` — let `input()` / `readline()` ask for input; see [Interactive input](../guides/interactive-input.md).
- `stopOnError: true` — if this execution fails, every `execute()` still **queued behind it** is skipped: it resolves with `success: false`, `aborted: true`, `status: 'aborted'`, empty `output`, and never reaches the kernel. Without it a failure does not affect the queue.
- `userExpressions: { name: 'expr' }` — evaluated in the kernel after the code, only if it succeeded; results in `result.userExpressions[name]`, each `{ status: 'ok', data, metadata }` or `{ status: 'error', ename, evalue, traceback }`.
- `silent`, `storeHistory` — passed through to the kernel (`silent` suppresses `execute_input`, the execution count and history).

`ExecutionResult.output` holds every iopub message the execution produced (`stream`, `execute_result`, `display_data`, `update_display_data`, `clear_output`, `error`), in arrival order. It does **not** contain `status` messages.

### `interrupt(options?: { timeout?: number }): Promise<boolean>`

Sends `interrupt_request` on the control channel. Resolves `true` if the kernel acknowledged (`interrupt_reply` with `status: 'ok'`), `false` if not within `timeout` (default 5 000 ms) or the session is gone — it **never rejects**. The interrupt is real: the kernel services its control channel on a watcher thread while code runs, so it is answered immediately even mid-execution and running code is broken out of like Ctrl-C (R: an interrupt condition; Python: `KeyboardInterrupt`). The interrupted `execute()` resolves with `success: false`; the kernel stays usable. Interrupting an idle kernel does nothing. See [Interrupting](../guides/interrupting.md) for limits.

### `sendInputReply(value: string): void`

Answers a pending `'input_request'`. Fire-and-forget; what comes back is the running execution finishing.

### `createShiny(options: ShinyAppOptions): Promise<ShinyAppHandle>`

R only. Runs `shiny::runApp()` in the session (with `timeout: 0`) and resolves once the port accepts connections; the returned `done` promise resolves when the app stops. The session is busy for as long as the app runs.

## Protocol requests

Each is a real Jupyter request answered by the kernel; each rejects on an `error`/`aborted` status, on timeout (10 s), or if the session ends. They are queued **behind a running execution** on the kernel's main thread (only `interrupt()` is serviced during one).

| Method | Sends | Resolves with |
|---|---|---|
| `complete(code, cursorPos = code.length)` | `complete_request` | `{ status, matches: string[], cursor_start, cursor_end, metadata }` |
| `inspect(code, cursorPos = code.length, detailLevel = 0)` | `inspect_request` | `{ status, found, data: { 'text/plain'?, 'text/html'? }, metadata }` |
| `isComplete(code)` | `is_complete_request` | `{ status: 'complete' \| 'incomplete' \| 'invalid' \| 'unknown', indent? }` |
| `kernelInfo()` | `kernel_info_request` | `{ protocol_version, implementation, implementation_version, language_info: { name, version, … }, banner, … }` |
| `commInfo(targetName?)` | `comm_info_request` | `{ status, comms: { [commId]: { target_name } } }` |
| `queryKernelHistory(options?)` | `history_request` | `KernelHistoryEntry[]` — see [History](../guides/history.md) |
| `request<T>(msgType, content?, { timeout? })` | any whitelisted request/reply pair | the reply `content` |

`request()` is the generic form behind the methods above. It accepts the request types in the [whitelist](../protocol.md#client--themisto); the reply type is derived by replacing `_request` with `_reply`. It is not for `execute_request`, `input_reply` or `shutdown_request`.

## Comms

A *comm* is a named message stream between the client and a target registered inside the kernel (R: `hera::CommManager$register_comm_target(name, callback)`). Carpo has no way to register targets yet, so comms are an R feature today. See [the guide](../guides/comms.md).

| Method | Description |
|---|---|
| `openComm(targetName, data?): Promise<Comm>` | Opens a comm and returns a `Comm` object. If the kernel has no such target it answers with a `comm_close`, so the returned comm emits `'close'`. |
| `commOpen(targetName, data?, commId?)` | Low-level: sends `comm_open`; resolves `{ commId, msgId }`. |
| `commMsg(commId, data?)` / `commClose(commId, data?)` | Low-level: send `comm_msg` / `comm_close`; resolve with the msg id they were sent under. |

**`Comm`** (`EventEmitter`)

| Member | Description |
|---|---|
| `id`, `targetName`, `closed` | Identity and state. |
| `send(data?): Promise<string>` | Sends a `comm_msg`. Rejects if the comm is closed. |
| `close(data?): Promise<void>` | Sends `comm_close` (no-op if already closed) and emits `'close'`. |
| event `'message'` `(data)` | The kernel sent a `comm_msg` on this comm. |
| event `'close'` `(data)` | Closed by either side, **or** because the kernel restarted, exited, or the session stopped — then `data.reason` is `'kernel restarted'`, `'kernel exited'`, `'session stopped'` or `'connection lost'`. |

Comms the **kernel** opens arrive as the session's `'comm'` event: `session.on('comm', (comm, data) => …)` where `data` is the `comm_open`'s payload.

## History

| Method | Description |
|---|---|
| `getHistory(): ExecutionHistoryEntry[]` | This session object's own transcript: `{ code, executionCount?, time, messages, truncated? }` per `execute()`, output included. In memory, capped at the latest **200** entries; within an entry the stream (stdout/stderr) text is bounded to the newest **~500 000 characters** (older stream messages are dropped and `truncated` is `true`; results, display data and errors are kept in full). Survives `restart()`, gone with the `Session` object. It is the live array — copy it if you need a snapshot. |
| `queryKernelHistory(options?)` | What the *kernel* remembers running (inputs only; lost when the kernel restarts). See [History](../guides/history.md) for the difference. |

## Status

### `status(): Promise<SessionStatusInfo>`

What the supervisor knows about this session's kernel process right now — a plain `GET {httpBase}/sessions/{sessionId}`, so it needs no kernel round trip:

```typescript
const s = await session.status();
// { sessionId, status: 'ready', kernelType: 'r', workingDirectory: '...', pid: 4242,
//   memoryBytes: 88000000, heartbeat: { hasPong: true, rttMs: 0.6, sinceLastPongMs: 42, misses: 0 } }
```

`heartbeat` is the supervisor's ZMQ ping to the kernel (about ten a second): `rttMs` is the round trip of the last answered ping, `sinceLastPongMs` how long ago that was, `misses` how many pings in a row went unanswered (`0` = healthy). The kernel answers pings from a thread of its own, separate from the one that runs code, so **the heartbeat stays live while the kernel is busy** — unlike `kernelInfo()`, which would wait for the running cell to finish. Use it as a liveness signal ("is it alive?"), not a responsiveness one ("is it free?" — that is `executionState`). `heartbeat` is `null` for a session with no client (e.g. one that has been stopped); `hasPong` is `false` until the first ping has been answered. Rejects if the supervisor cannot be reached or no longer knows the session (a stopped session is removed).

## Lifecycle

### `restart(options?: Partial<EngineOptions>): Promise<void>`

Replaces the kernel process under the **same session id** — recovers a crashed session, or resets a healthy one (Jupyter's "Restart Kernel"). Queued and running `execute()` calls reject with `Queue cleared`; pending requests reject with `Session is restarting`; open comms emit `'close'` with reason `'kernel restarted'`.

`options`, if given, are **merged** with the session's current options (`{ ...session.options, ...options }`) before being sent, so switching only `rHome` keeps `workingDirectory`, `rLibs`, …; `session.options` is updated. To *clear* a field, pass `undefined` or an empty string. The old kernel receives `shutdown_request{restart: true}` — you can observe the kernel's `shutdown_reply` (`restart: true`) as an event before `'restarted'` fires. Throws if the session was already stopped or killed.

### `stop(): Promise<void>`

Sends the kernel `shutdown_request{restart: false}` through the supervisor, waits for the process to exit (force-killed after ~2 s if it does not), waits up to 250 ms for the `shutdown_reply` to be observable, closes the socket and emits `'stopped'`. Idempotent. Pending work rejects (`Queue cleared`, `Session stopped`).

### `kill(): void`

Closes the local socket and rejects pending work **without** telling the supervisor — only for cleanup on the way out; the kernel process is not stopped by this alone (`SessionManager.killAll()` also kills the supervisor, which takes the kernels with it).

### `ready(): Promise<void>`

Resolves once the session's WebSocket is open and bound. `execute()` and the request methods await it for you.

## Events

All are Node `EventEmitter` events on the `Session`.

| Event | Arguments | When |
|---|---|---|
| `'message'` | `JupyterMessage` | Every message from the kernel (iopub, shell, control, stdin). |
| `'*'` | `(msgType, content)` | Same, as a catch-all. |
| *`<msg_type>`* — e.g. `'status'`, `'execute_reply'`, `'shutdown_reply'`, `'input_request'`, `'comm_msg'`, `'display_data'`, `'update_display_data'`, `'clear_output'` | `content` | Every message also fires an event named after its `msg_type`. |
| `'stdout'`, `'stderr'` | `text` | Streamed output (`stream` messages by `name`). |
| `'result'` | `text` | The `text/plain` of each `execute_result` / `display_data`. |
| `'input_request'` | `{ prompt, password }` | The kernel is blocked waiting for input; answer with `sendInputReply()`. |
| `'comm'` | `(comm: Comm, data)` | The kernel opened a comm. |
| `'error'` | `string` (the error's `evalue`) or `Error` | A code error in the kernel, or an internal failure handling a message. **Requires a listener.** |
| `'exit'` | `{ reason }` | Kernel died unexpectedly (or the supervisor connection dropped). The session is left recoverable via `restart()`. Not emitted for `stop()` / `restart()`. |
| `'restarted'` | — | `restart()` finished. |
| `'stopped'` | — | `stop()` finished. |
| `'requestError'` | `{ id, error }` | The supervisor refused a fire-and-forget request (a `comm_*` message) — e.g. session not found. |
