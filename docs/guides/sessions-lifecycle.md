# Session lifecycle

```
              createSession()                     restart()
  (nothing) ─────────────────► ready ◄──────────────────────────────┐
                                │  │                                │
                    stop()      │  │  kernel dies / heartbeat lost  │
              ┌─────────────────┘  └──────────────► crashed ────────┘  (restart() recovers it,
              ▼                                        ('exit' event)     same session id)
           stopped   (terminal — create a new session)
```

Supervisor-side session `status` values: `starting`, `ready`, `stopped`, `crashed`.

## Creating

```typescript
const session = await manager.createSession({
    kernelType: 'r',
    rHome: '/usr/lib/R',
    workingDirectory: '/projects/analysis',   // where the kernel starts; must exist
});
```

`createSession()` resolves once the kernel process has started, registered with the supervisor (up to 60 s) and the session's WebSocket is open. It rejects with the underlying reason if not (see [Troubleshooting](../troubleshooting.md)). Each session is its own OS process — create as many as you like; they share only the supervisor.

## Stopping

`await session.stop()`:

1. The client asks the supervisor to stop the session (`DELETE /sessions/:id`).
2. The supervisor sends the kernel a real Jupyter `shutdown_request` with `restart: false` on the control channel.
3. It waits up to about **2 seconds** for the kernel to exit by itself, force-killing it if it does not (a kernel busy inside `shiny::runApp()` never gets to process the request — force-kill is the normal path there, not a fault).
4. The kernel's `shutdown_reply` (and its iopub `shutdown` message) reach your `Session` and are emitted as the `'shutdown_reply'` event; then `'stopped'` fires.

An orderly stop is **never** reported as a crash (`'exit'` is not emitted). A stopped session is gone from the supervisor; `restart()` on it throws.

`SessionManager.stopAll()` stops every session and then kills the supervisor. Call it before exiting — otherwise the process hangs, held open by the supervisor's pipes. `killAll()` is the hard fallback.

## Restarting

```typescript
await session.restart();                                    // same options, fresh interpreter
await session.restart({ rHome: '/opt/R/4.6.0' });          // switch R installations in place
```

- The kernel gets `shutdown_request` with `restart: true` (observe it as `'shutdown_reply'` with `restart === true`), a new kernel starts under the **same session id**, and the WebSocket reconnects to the same URL. `'restarted'` fires when it is ready.
- **Options are merged.** `restart(options)` sends `{ ...session.options, ...options }`: you only pass what changes; `workingDirectory`, `rLibs`, `heraSrcPath`, … are kept. `session.options` reflects the new values afterwards. (The supervisor's own restart endpoint replaces options wholesale; the merge happens in the client.) To clear a field, pass `undefined` or `''`.
- The interpreter is brand new: variables, loaded packages and the kernel's history are gone. `session.getHistory()` (client-side) and your event listeners are kept.
- Queued and running `execute()` calls reject with `Queue cleared`; pending requests with `Session is restarting`; open comms emit `'close'` (`reason: 'kernel restarted'`).
- Restarting a session while another `restart()` for the same id is in flight waits its turn (the supervisor serialises operations per session id) — clicking "Restart" twice does not leak a kernel process.

## Crashes

A kernel that dies — killed from Task Manager, a segfault (e.g. `STATUS_ACCESS_VIOLATION` in a compiled R package), `os._exit()` / `quit()` — is detected by the supervisor within milliseconds (it watches the OS process handle) and you get:

- the `'exit'` event with `{ reason }` — e.g. `kernel process exited unexpectedly (process exited with code 0xc0000005 (STATUS_ACCESS_VIOLATION -- a native crash, e.g. in a compiled R package))`;
- every queued and running `execute()` rejects with `Queue cleared`, pending requests reject with `Session process exited: <reason>`, open comms emit `'close'` (`reason: 'kernel exited'`).

A kernel that is *alive but unresponsive* (deadlocked, stuck in a native call) is caught later by the ZMQ heartbeat — three missed 20-second pings, so roughly a minute — and reported the same way with a `heartbeat gave up waiting for a response` reason.

Recover with `await session.restart()`: same session id, fresh kernel. An `'exit'` is also emitted if the WebSocket to the supervisor drops (`WebSocket connection to the supervisor closed unexpectedly`) — that usually means the supervisor process itself ended, in which case you need a new `SessionManager`.

## Timeouts vs. kernel state

`execute()`'s `timeout` only stops *your promise* — the kernel keeps running the code. After a timeout, call `interrupt()` (see [Interrupting](interrupting.md)) or `restart()` before sending more work, or the next `execute()` queues behind the still-running one.

## Multiple sessions

Sessions are isolated processes; the supervisor multiplexes them. One session blocked in a long call does not delay another (see `examples/advanced/two-sessions.js`). Each has its own kernel state, working directory, R library path, Python interpreter and environment.
