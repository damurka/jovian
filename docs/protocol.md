# Protocol reference

Two protocols matter in Jovian:

1. **Jupyter messaging over ZMQ** between Themisto and each kernel (`elara` / `carpo`) — the standard protocol, version **5.6** (what `kernel_info_reply.protocol_version` reports).
2. **Themisto's HTTP + WebSocket API** between the TypeScript client (`lib/`) and Themisto — Jovian's own, thin, JSON API. You normally never touch it directly (`Session` does), but it is stable enough to script against.

## 1. Jupyter messages the kernels support

Handlers are registered in `KernelCore` (`native/src/adrastea/core/kernel/kernel_core.cpp`) and are the same for Elara and Carpo; the language-specific part is delegated to the interpreter.

| Message | Channel | Reply | Notes |
|---|---|---|---|
| `execute_request` | shell | `execute_reply` | Fields honoured: `code`, `silent`, `store_history`, `allow_stdin`, `stop_on_error`, `user_expressions`. Output arrives on iopub while it runs. |
| `complete_request` | shell | `complete_reply` | `matches`, `cursor_start`, `cursor_end`. |
| `inspect_request` | shell | `inspect_reply` | `found`, `data` (mime bundle). `detail_level` is accepted but ignored by both kernels. |
| `is_complete_request` | shell | `is_complete_reply` | `status`: `complete` / `incomplete` / `invalid` (Carpo may answer `unknown` if its helper fails). |
| `kernel_info_request` | shell | `kernel_info_reply` | Adds `protocol_version`. |
| `history_request` | shell | `history_reply` | `hist_access_type` = `tail` (default) / `range` / `search`; see [History](guides/history.md). |
| `comm_info_request` | shell | `comm_info_reply` | Optional `target_name` filter. |
| `comm_open` / `comm_msg` / `comm_close` | shell (client → kernel); iopub (kernel → client) | — | See [Comms](guides/comms.md). |
| `interrupt_request` | control | `interrupt_reply` | Serviced **while code runs** (see [Architecture](architecture/overview.md#inside-a-kernel-process-elara--carpo)). |
| `shutdown_request` | control | `shutdown_reply` | Content `{restart}`; the reply echoes it. The kernel also publishes an iopub `shutdown` message. |
| `input_request` (kernel → client) / `input_reply` | stdin | — | Only sent when the `execute_request` had `allow_stdin: true`. |

iopub messages the kernels publish: `status` (`busy` / `idle` around **every** request the kernel handles, not just executes; `starting` once at start-up), `execute_input`, `stream` (`stdout` / `stderr`), `display_data`, `update_display_data`, `clear_output`, `execute_result`, `error`, `comm_open` / `comm_msg` / `comm_close`, `shutdown`, `interrupt`, and an `iopub_welcome`.

**`stream` messages are coalesced.** A `print()` in a loop is one or two writes per line, and publishing each write on its own would flood ZMQ, the relay and the client far faster than they drain. The interpreter base class (`adrastea::Interpreter::publishStream`) therefore buffers stdout/stderr text and publishes it as **one `stream` message at most every ~50 ms, or as soon as 16 KB have accumulated** (the first write after a quiet period goes out at once). A different stream (stdout → stderr) starts a new message. Ordering is preserved: the buffer is flushed before any other output (`execute_result`, `display_data`, `update_display_data`, `error`, `clear_output`), before an `input_request` (so a prompt printed just before it comes first), and before the `execute_reply`; a small flusher thread flushes stale text while the interpreter is busy computing without writing. Chunk boundaries are therefore up to the kernel: a token such as `tick 3` may be split across two messages, so match on accumulated text.

### `execute_request` details

- `stop_on_error: true` — if this request fails, requests already queued behind it on the kernel's shell socket get a reply with `status: "aborted"` instead of running. (The TypeScript `ExecutionQueue` is single-flight, so it additionally aborts *its own* queued items — see [the API reference](api/session.md).)
- `user_expressions` — `{name: "expression"}`, evaluated **after** the code and **only if it succeeded**; the reply's `user_expressions` holds `{name: {status: "ok", data: {"text/plain": …}, metadata: {}}}` or `{name: {status: "error", ename, evalue, traceback: []}}` per expression. R evaluates in the global environment via `print()` capture; Python via `eval()` + `repr()`.
- `silent: true` — no `execute_input`, no execution-count increment, no history entry.
- Kernel replies use `status`: `ok`, `error` (with `ename`, `evalue`, `traceback`), or `aborted`.

### How each kernel implements the language-specific requests

| Request | Elara (R) | Carpo (Python) |
|---|---|---|
| execute | `hera:::hera_call("execute", …)` — `evaluate` with output handlers | `__carpo_run(code, globals)` in the bootstrap module (`ast` split; last expression auto-displayed) |
| complete | `hera` → `utils:::.completeToken` | `rlcompleter` |
| inspect | `hera` — class, printed form and help (HTML + text) | `inspect.signature`, `Type:`, docstring or `repr` (text/plain only) |
| is_complete | `R_ParseVector` status | `codeop.compile_command` |
| `kernel_info` | `implementation: "xr"`, `language_info.name: "R"`, version = R's | `implementation: "carpo"`, `language_info.name: "python"`, version = Python's, `banner: "carpo (Python x.y.z)"` |
| interrupt | sets `R_interrupts_pending` / `UserBreak` | real SIGINT → `KeyboardInterrupt` |
| comms | full: `hera::CommManager`, `Comm` | none — nothing in the bootstrap registers comm targets, so every `comm_open` is answered with a `comm_close` and `comm_info` is empty |

## 2. Themisto's HTTP API

Base URL `http://127.0.0.1:<httpPort>`; both ports are announced on Themisto's stdout at start-up as one JSON line:

```json
{"type":"supervisorReady","httpPort":51662,"wsPort":58722}
```

Bodies are JSON. Errors are `{"error": "<message>"}`.

### `POST /sessions` — create

Request body (every field optional; unset means "empty"):

| Field | Type | Meaning |
|---|---|---|
| `kernelType` | `"r"` \| `"python"` | Default `"r"`. A type with no kernel executable available fails just this call. |
| `rHome`, `rPath`, `rLibs` | string | R installation (`R_HOME`), directory containing `R.dll` (Windows), extra library path (`R_LIBS`). |
| `pandocPath` | string | Directory of a pandoc binary (`RSTUDIO_PANDOC`, added to `PATH`). |
| `heraSrcPath` | string | Source of the `hera` package, installed on demand (`ELARA_HERA_SRC`). |
| `pythonHome`, `pythonPath`, `venvPath` | string | Python prefix (`PYTHONHOME`), extra `PYTHONPATH`, venv whose `site-packages` is added to `sys.path`. |
| `workingDirectory` | string | Directory the kernel process starts in. Must exist. |

Responses:

- **200** — the session object: `{"sessionId", "status", "kernelType", "workingDirectory", "pid", "memoryBytes", "heartbeat"}`. `status` is `starting` \| `ready` \| `stopped` \| `crashed`; `memoryBytes` is `null` when unavailable; `heartbeat` is `{"hasPong", "rttMs", "sinceLastPongMs", "misses"}` (see below) or `null` when the session has no client.
- **400** — `{"error":"invalid JSON body"}`.
- **500** — `{"error": …}`: no kernel executable for that `kernelType`; `workingDirectory does not exist or is not a directory: <path>`; the kernel failed to register (R not found, …).

### `GET /sessions` and `GET /sessions/:id`

`{"sessions":[<session object>, …]}` and `<session object>`; **404** `{"error":"session not found"}` for an unknown id. `Session.status()` is a `GET /sessions/:id`.

The session object's `heartbeat` reports the supervisor's ping to the kernel's heartbeat channel: `hasPong` (false until the first ping is answered), `rttMs` (round trip of the last answered ping), `sinceLastPongMs` (age of that answer) and `misses` (consecutive unanswered pings; `0` is healthy). The kernel answers from a thread separate from the one running code, so it stays fresh while a cell runs; it is a liveness signal, not a "kernel is free" signal.

### `DELETE /sessions/:id` — stop

Sends the kernel a `shutdown_request` with `restart: false`, waits up to ~2 s for a clean exit, then force-kills, and removes the session. **200** `{"sessionId","status":"stopped"}`; **404** if unknown. A stopped session id is gone for good.

### `POST /sessions/:id/restart` — restart

Optional body, same shape as create. **Without a body** the session's original options are reused; **with one** it replaces them *wholesale* — unset fields become empty, they are *not* merged with the old values. (`Session.restart(options)` in `lib/` does the merge for you before sending.) The old kernel gets `shutdown_request{restart:true}`; a new kernel starts under the **same session id**. **200** `{"sessionId","status":"ready"}`; **500** `{"error"}`.

## 3. Themisto's WebSocket API

Connect to `ws://127.0.0.1:<wsPort>/sessions/<sessionId>/messages`. An unknown session id closes the socket with code **1008** (`unknown session`). At most one connection per session is relayed to at a time: the most recent one owns the kernel's output stream. All frames are JSON text.

### Client → Themisto

| Frame | Fields | Effect |
|---|---|---|
| `execute` | `id`, `code`, `options` (`silent`, `storeHistory`, `allowStdin`, `stopOnError`, `userExpressions`) | Sends `execute_request` on shell with `msg_id = id`. Other option keys (e.g. `timeout`) are ignored natively. |
| `inputReply` | `value` | Sends `input_reply` on the stdin channel — the only thing that unblocks a pending `input()` / `readline()`. |
| `request` | `id`, `channel` (`"shell"` \| `"control"`), `msgType`, `content` | Sends any **whitelisted** request with `msg_id = id`. |

`request` whitelist — anything else is refused:

| Channel | Allowed `msgType` |
|---|---|
| shell | `complete_request`, `inspect_request`, `is_complete_request`, `kernel_info_request`, `history_request`, `comm_info_request`, `comm_open`, `comm_msg`, `comm_close` |
| control | `interrupt_request` |

`execute_request` (own frame: it owns stdin/history semantics), `input_reply` (own frame) and `shutdown_request` (a lifecycle operation — a raw one would kill the kernel behind the supervisor's back; use `DELETE` / `restart`) are deliberately excluded. Malformed frames are ignored.

### Themisto → client

| Frame | Fields | Meaning |
|---|---|---|
| `ready` | — | Sent right after the connection opens and the session is bound. |
| `message` | `channel`, `topic`, `msg_type`, `parent_msg_id`, `content` | Every kernel message on iopub, shell, control or stdin. `channel` is `"iopub"` \| `"shell"` \| `"control"` \| `"stdin"`. `parent_msg_id` is the `id` of the frame that caused it, which is how replies are correlated. |
| `kernelExit` | `reason` | The kernel process died unexpectedly (`kernel process exited unexpectedly (process exited with code 0x…)`) or its heartbeat gave up. Never sent for a requested stop/restart. |
| `requestError` | `id`, `error` | A `request` frame could not be sent: `'<type>' is not an allowed request on the '<channel>' channel`, or `session not found`. No reply will follow. |
| `log` | `level`, `message`, `data` | Handled by the TypeScript client (replayed through its logger); the native supervisor does not currently emit it. |

Example — `complete_request` round trip:

```json
→ {"type":"request","id":"7f3…","channel":"shell","msgType":"complete_request","content":{"code":"pri","cursor_pos":3}}
← {"type":"message","channel":"iopub","topic":"kernel_core.<kernel>.status","msg_type":"status","parent_msg_id":"7f3…","content":{"execution_state":"busy"}}
← {"type":"message","channel":"shell","topic":"complete_reply","msg_type":"complete_reply","parent_msg_id":"7f3…","content":{"status":"ok","matches":["print"],"cursor_start":0,"cursor_end":3,"metadata":{}}}
← {"type":"message","channel":"iopub","topic":"kernel_core.<kernel>.status","msg_type":"status","parent_msg_id":"7f3…","content":{"execution_state":"idle"}}
```

## 4. Ordering guarantees (and their absence)

- Messages on **one** ZMQ channel arrive in order. Across channels there is **no** ordering guarantee: iopub output published before an `execute_reply` can be relayed *after* it. The TypeScript `ExecutionQueue` compensates with a short (50 ms) grace wait when an `ok` reply arrives with no output collected yet.
- Requests other than `interrupt_request` are answered by the kernel's main thread, so they queue behind a running execution.
