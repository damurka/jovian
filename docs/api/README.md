# TypeScript API reference

The `@damurka/jovian` package (ES modules) exports:

```typescript
import {
    SessionManager, Session, Comm,          // classes
    type EngineOptions, type ExecutionOptions, type ExecutionResult, type JupyterMessage,
    // …every type in ./types (see types.md), plus the middleware classes
} from '@damurka/jovian';
```

| Page | Contents |
|---|---|
| **This page** | `SessionManager`, events, errors and timeouts |
| [`session.md`](session.md) | `Session` — every method, property and event; `Comm` |
| [`types.md`](types.md) | `EngineOptions`, `ExecutionOptions`, `ExecutionResult`, reply content types, message types |

## `SessionManager`

Creates sessions and owns the one shared supervisor process (`themisto`) they all talk to. The supervisor is spawned lazily on the first `createSession()` and killed by `stopAll()` / `killAll()` (and, as a safety net, when the Node process exits).

```typescript
const manager = new SessionManager();
```

| Member | Description |
|---|---|
| `createSession(options?: EngineOptions): Promise<Session>` | Spawns a kernel process (per `options.kernelType`), waits until it has registered and the session's WebSocket is ready, and resolves with the `Session`. Rejects with the supervisor's message if the kernel could not start: `workingDirectory does not exist or is not a directory: …`, `no kernel executable is configured for kernelType 'python' …`, or — when the kernel process itself failed (R or Python not found, …) — `Kernel process exited before it could register -- check its stderr output for the actual error.` The *actual* R/Python error is printed on stderr with an `[elara]` / `[carpo]` prefix (see [Troubleshooting](../troubleshooting.md)). |
| `stopAll(): Promise<void>` | Gracefully `stop()`s every session, then kills the supervisor. Call this before your process exits — **without it a script hangs**, because the supervisor's pipes keep Node's event loop alive. |
| `killAll(): void` | Immediately force-closes every session and kills the supervisor. Use it as the fallback when `stopAll()` is racing a timeout (a kernel stuck inside `shiny::runApp()` cannot process a shutdown until that call returns). |

The kernel binaries are looked up in this order: `$JOVIAN_NATIVE_DIR`; the installed `@damurka/jovian-<os>-<cpu>` package (its `bin/` directory); `dist/native/Release` in a source checkout. If none has `themisto[.exe]`, `createSession()` rejects with an error naming what was expected (`jovian: the kernel binaries were not found. Expected the '@damurka/jovian-…' package …`, or, on a platform without a prebuilt package, the list of supported ones). When the library runs from an installed package and `heraSrcPath` is not given, it defaults to the copy of `hera` shipped in the package.

## Events

`Session` is an `EventEmitter`. Everything the kernel sends is available as events — the full list is in [`session.md`](session.md#events). The two you must know about:

- **Always attach an `'error'` listener.** `Session` re-emits every R/Python error message as Node's special `'error'` event (with the error message string), and internal failures as an `Error` instance. Node **throws** if `'error'` is emitted with no listener, which kills the process. The error is also in the execution's `ExecutionResult` — the listener can be a no-op.
- `'exit'` means the kernel (or the connection to the supervisor) died unexpectedly.

## Errors and timeouts

| Situation | What you get |
|---|---|
| R/Python code raises an error | `execute()` **resolves** with `success: false`, the `error` message, and the `error` iopub message in `output` (with `ename`, `evalue`, `traceback`). It does not reject. |
| `execute()` exceeds its `timeout` (default **30 000 ms**; `0` = none) | Rejects with `Execution timed out after <n>ms`. The kernel is *not* interrupted — call `interrupt()` yourself if you want the code to stop. |
| A kernel waits for `input()` / `readline()` | The execution's timeout is cleared while it waits, so a slow human cannot trip it. |
| Session stopped, restarted, killed or crashed while an `execute()` is queued or running | Rejects with `Queue cleared`. |
| More than `queueSize` (default **100**) executions queued | Rejects with `Execution queue is full`. |
| Protocol requests (`complete()`, `kernelInfo()`, …) | Time out after **10 000 ms** by default (`session.request(…, { timeout })` to change), reject on an `error`/`aborted` reply, on a supervisor `requestError`, and with `Session stopped` / `Session process exited: …` / `Session is restarting` / `Session was killed` if the session goes away. |
| `interrupt()` | Never rejects: resolves `false` if unacknowledged within its timeout (default 5 000 ms). |
| `restart()` after `stop()`/`kill()` | Throws `Cannot restart session <id>: it was already stopped`. A stopped session is terminal — create a new one. |

Logging: pass `logger` (a `(level, message, data?) => void` function) to route library logs; without it every level, including `trace`/`debug`, is printed to the console. `enableLogging` adds a middleware that prints the first 100 characters of every raw message frame; `enableMetrics` prints a messages/second line per message.
