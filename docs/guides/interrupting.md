# Interrupting running code

`session.interrupt()` stops code that is currently running — a `Sys.sleep(60)`, a `while (TRUE) {}` loop, a Python `time.sleep(60)`, a `while True: pass` — the same way pressing Ctrl-C in a terminal would. The kernel keeps running and accepts the next `execute()`.

```typescript
const running = session.execute('Sys.sleep(60)', { timeout: 0 });
setTimeout(async () => {
    const acknowledged = await session.interrupt();     // true — answered while the sleep is still running
    console.log('acknowledged:', acknowledged);
}, 1000);

const result = await running;                           // resolves within about a second
console.log(result.success);                            // false — the execution was interrupted
```

## What you get back

| | |
|---|---|
| `interrupt()` | `Promise<boolean>`: `true` if the kernel answered `interrupt_reply` with `status: 'ok'`, `false` if it did not within the timeout (default 5 000 ms, `interrupt({ timeout })`) or the session is gone. **Never rejects.** |
| The interrupted `execute()` | Resolves (not rejects) with `success: false` and an `error` message in `output`. R reports an interrupt/user-break condition; Python reports `KeyboardInterrupt` (`ename: 'KeyboardInterrupt'`). |
| Interrupting an idle kernel | Acknowledged (`true`) and otherwise a no-op. It does **not** affect the next execution. |

If you gave the execution a `timeout` (default 30 s) and it fires first, `execute()` rejects with `Execution timed out after <n>ms (the kernel was interrupted)` — **the library interrupts the kernel for you**, so the code stops instead of running on unseen. Pass `interruptOnTimeout: false` to reject without interrupting; then the kernel keeps running your code (and everything after it, including `complete()` / `inspect()`, waits behind it) until you call `interrupt()` yourself or `restart()`.

## How it works

The kernel executes code on its main thread, which is also the thread that reads sockets, so normally a control message would wait for the running code to finish. Instead, while code runs, a **control watcher thread** reads the control socket and handles `interrupt_request` immediately (details in [Architecture](../architecture/overview.md#inside-a-kernel-process-elara--carpo)):

- **R** — sets R's pending-interrupt flag (`R_interrupts_pending` on Linux/macOS, `UserBreak` on Windows). R notices at its next `R_CheckUserInterrupt()`, which its evaluator and its `Sys.sleep()` call regularly, and unwinds to top level.
- **Python** — delivers a real `SIGINT` to the interpreter thread (`raise(SIGINT)` on Windows, `pthread_kill` on POSIX), which Python's own handler turns into `KeyboardInterrupt` and which also wakes a blocked `time.sleep()`. (The bootstrap installs `signal.default_int_handler` explicitly, because a process launched by a supervisor may have started with SIGINT ignored.) `PyErr_SetInterrupt()` alone would not wake `time.sleep()`.

Other control messages that arrive during an execution (for example a `shutdown_request` from `stop()`) are queued and handled after it finishes, in order.

## Limits

- **Native code that does not return to the interpreter.** A long C-extension call, a blocking socket read, or R code stuck in compiled code that never calls `R_CheckUserInterrupt()` is only interrupted once it returns control.
- **Blocked on input.** A kernel waiting for an `input_request` answer is blocked in a ZMQ receive; answer it (or `restart()`) first.
- **A stuck kernel** that ignores the interrupt: `restart()` it — or `stop()`, which force-kills after about 2 s.
- **Race at the very end.** An interrupt sent in the instant an execution is finishing can land just after it and either be ignored or surface as a `KeyboardInterrupt` in the next moment of Python code.
- The POSIX implementation (Linux/macOS) is the same design but was written after the Windows one was verified; CI is what exercises it there.

## Jupyter frontends

The generated kernelspecs declare `"interrupt_mode": "message"`, so a frontend such as JupyterLab interrupts by sending `interrupt_request` on the control channel — which now genuinely interrupts.
