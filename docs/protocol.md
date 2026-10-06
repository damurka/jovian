# Protocol reference

Two protocols matter in Jovian:

1. **Jupyter messaging over ZMQ** between Themisto and each kernel (`elara` / `carpo` / `callisto`) — the standard protocol, version **5.6** (what `kernel_info_reply.protocol_version` reports).
2. **Themisto's HTTP + WebSocket API** between the TypeScript client (`lib/`) and Themisto — Jovian's own, thin, JSON API. You normally never touch it directly (`Session` does), but it is stable enough to script against.

## 1. Jupyter messages the kernels support

Handlers are registered in `KernelCore` (`native/src/adrastea/core/kernel/kernel_core.cpp`) and are the same for every kernel; the language-specific part is delegated to the interpreter.

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

What the coalescing buys, measured with Elara on Windows (a flood of 20 000 `cat()` lines; a line every 100 ms):

| Setting | Flood | Messages | Delay of a trickled line (median / max) |
|---|---|---|---|
| none (every write its own message) | 50 000 lines: 17.7 s | ~5 per line | -- |
| 10 ms / 4 KB | 167 ms | 59 | 10 / 20 ms |
| **50 ms / 16 KB (default)** | **166 ms** | **15** | **12 / 19 ms** |
| 250 ms / 256 KB | 176 ms | 1 | 144 / 266 ms |

Without it, R's several writes per line become several messages each, about 70x slower on a flood; beyond the default, bigger batches save nothing measurable and make live output lag. Both limits can be changed for such measurements through `JOVIAN_STREAM_FLUSH_MS` and `JOVIAN_STREAM_FLUSH_BYTES` in the environment the kernel starts in. Other message types (`display_data`, `update_display_data`, ...) are not coalesced.

### `execute_request` details

- `stop_on_error: true` — if this request fails, requests already queued behind it on the kernel's shell socket get a reply with `status: "aborted"` instead of running. (The TypeScript `ExecutionQueue` is single-flight, so it additionally aborts *its own* queued items — see [the API reference](api/session.md).)
- `user_expressions` — `{name: "expression"}`, evaluated **after** the code and **only if it succeeded**; the reply's `user_expressions` holds `{name: {status: "ok", data: {"text/plain": …}, metadata: {}}}` or `{name: {status: "error", ename, evalue, traceback: []}}` per expression. R evaluates in the global environment via `print()` capture; Python via `eval()` + `repr()`.
- `silent: true` — no `execute_input`, no execution-count increment, no history entry.
- Kernel replies use `status`: `ok`, `error` (with `ename`, `evalue`, `traceback`), or `aborted`.

### How each kernel implements the language-specific requests

| Request | Elara (R) | Carpo (Python) | Callisto (Stata) |
|---|---|---|---|
| execute | `.jv.call("execute", …)` — each top-level expression evaluated with base R | `__carpo_run(code, globals)` in the bootstrap module (`ast` split; last expression auto-displayed) | the cell as a temporary do-file, `include`d; Stata's output buffer streamed; graphs exported as PNG |
| complete | `hera` → `utils:::.completeToken` | `rlcompleter` | Mata: variable names, global and local macros |
| inspect | `hera` — class, printed form and help (HTML + text) | `inspect.signature`, `Type:`, docstring or `repr` (text/plain only) | `describe` + `summarize` of a variable (text/plain) |
| is_complete | `R_ParseVector` status | `codeop.compile_command` | open `{`, `/*` or trailing `///` |
| `kernel_info` | `implementation: "xr"`, `language_info.name: "R"`, version = R's | `implementation: "carpo"`, `language_info.name: "python"`, version = Python's, `banner: "carpo (Python x.y.z)"` | `implementation: "callisto"`, `language_info.name: "stata"`, version = `c(stata_version)`, `banner: "callisto (Stata x.y ED)"` |
| interrupt | sets `R_interrupts_pending` / `UserBreak` | real SIGINT → `KeyboardInterrupt` | `StataSO_SetBreak()` → `r(1)` |
| comms | full: `.elara.CommManager`, `Comm` | none — nothing in the bootstrap registers comm targets, so every `comm_open` is answered with a `comm_close` and `comm_info` is empty | none, as for Carpo |

## 2. Themisto's HTTP API

Base URL `http://127.0.0.1:<httpPort>`; both ports, the access token and Themisto's process id are announced on its stdout at start-up as one JSON line:

```json
{"type":"supervisorReady","httpPort":51662,"wsPort":58722,"token":"<64 hex digits>","pid":4242}
```

Bodies are JSON. Errors are `{"error": "<message>"}`.

### Access

The API runs code and loads shared libraries from paths it is given, and 127.0.0.1 keeps other machines out but not other programs on this one. So (`native/src/themisto/access.hpp`):

- **Every request needs the token** from the ready line: `Authorization: Bearer <token>` on HTTP; on the WebSocket, `?token=<token>` in the URL (Node's WebSocket cannot send headers) or the same header. Without it: **401** `{"error":"missing or wrong access token"}`, and a WebSocket is closed with code **4401**.
- **Nothing from a web browser is accepted**, token or not: a request with an `Origin` header, which browsers always send and Node never does, gets **403** (WebSocket: close code **4403**). This stops a web page the user visits from reaching the API.
- The token is random per start, or the parent's choice through `JOVIAN_SUPERVISOR_TOKEN` in Themisto's environment (not its command line, which other users can read). `--no-auth` turns the check off; it exists for tests only.

Command-line options: `--kernel-exe`, `--python-kernel-exe`, `--stata-kernel-exe`, `--registration-ip`, `--idle-shutdown-minutes N` (exit, stopping every session, after N minutes with no WebSocket client connected and no request), `--no-auth`.

### `POST /sessions` — create

Request body (every field optional; unset means "empty"):

| Field | Type | Meaning |
|---|---|---|
| `kernelType` | `"r"` \| `"python"` \| `"stata"` \| `"ark"` | Default `"r"`. A type with no kernel executable available fails just this call. |
| `rHome`, `rPath`, `rLibs` | string | R installation (`R_HOME`), directory containing `R.dll` (Windows), extra library path (`R_LIBS`). |
| `pandocPath` | string | Directory of a pandoc binary (`RSTUDIO_PANDOC`, added to `PATH`). |
| `pythonHome`, `pythonPath`, `venvPath` | string | Python prefix (`PYTHONHOME`), extra `PYTHONPATH`, venv whose `site-packages` is added to `sys.path`. |
| `stataHome`, `stataEdition` | string | Stata directory and edition (`"mp"`, `"se"`, `"be"`). |
| `arkPath` | string | For `"ark"`: the ark executable (Posit's R kernel, not shipped with Jovian). R comes from `rHome` (`R_HOME`). |
| `workingDirectory` | string | Directory the kernel process starts in. Must exist. |

Responses:

- **200** — the session object: `{"sessionId", "status", "kernelType", "workingDirectory", "pid", "memoryBytes", "heartbeat", "options"}` (`options`: every create field it was started with, so a client reconnecting later can rebuild it). `status` is `starting` \| `ready` \| `stopped` \| `crashed`; `memoryBytes` is `null` when unavailable; `heartbeat` is `{"hasPong", "rttMs", "sinceLastPongMs", "misses"}` (see below) or `null` when the session has no client.
- **400** — `{"error":"invalid JSON body"}`.
- **500** — `{"error": …}`: no kernel executable for that `kernelType`; `workingDirectory does not exist or is not a directory: <path>`; the kernel failed to register (R not found, …).

### `GET /sessions` and `GET /sessions/:id`

`{"sessions":[<session object>, …]}` and `<session object>`; **404** `{"error":"session not found"}` for an unknown id. `Session.status()` is a `GET /sessions/:id`.

The session object's `heartbeat` reports the supervisor's ping to the kernel's heartbeat channel: `hasPong` (false until the first ping is answered), `rttMs` (round trip of the last answered ping), `sinceLastPongMs` (age of that answer) and `misses` (consecutive unanswered pings; `0` is healthy). The kernel answers from a thread separate from the one running code, so it stays fresh while a cell runs; it is a liveness signal, not a "kernel is free" signal.

### `DELETE /sessions/:id` — stop

Sends the kernel a `shutdown_request` with `restart: false`, waits up to ~2 s for a clean exit, then force-kills, and removes the session. **200** `{"sessionId","status":"stopped"}`; **404** if unknown. A stopped session id is gone for good.

### `POST /sessions/:id/restart` — restart

Optional body, same shape as create. **Without a body** the session's original options are reused; **with one** it replaces them *wholesale* — unset fields become empty, they are *not* merged with the old values. (`Session.restart(options)` in `lib/` does the merge for you before sending.) The old kernel gets `shutdown_request{restart:true}`; a new kernel starts under the **same session id**. **200** `{"sessionId","status":"ready"}`; **500** `{"error"}`.

### `POST /shutdown` — stop the supervisor

Stops every session (as `DELETE` would) and then Themisto itself. **200** `{"status":"shutting down"}`, sent before the stopping. `SessionManager.stopAll()` uses it for a persistent supervisor.

### Kernel registration (JEP 66)

A session is reported ready once the kernel has confirmed the supervisor's subscription to its output (the `iopub_welcome` of JEP 65), so nothing the first cell prints can be published before the supervisor listens; a kernel that sends none (not every Jupyter kernel does) is given half a second. A kernel Themisto starts binds its own ports and reports them on the registration socket (the ports it is bound to, never ones chosen beforehand). Two forms are accepted: Adrastea's own (a signature frame and a JSON object with `kernel_id` -- the `--registration-id` the kernel was started with -- and the ports as strings, answered with a signed `ACK`; one whose signature does not check, or whose `kernel_id` is not the launch being waited for, is answered with a signed `REJECTED <why>` and the kernel ends), and the standard Jupyter handshake of [JEP 66](https://github.com/jupyter/enhancement-proposals/pull/66) that other kernels, such as Posit's Ark, use: a complete signed `handshake_request` message from a REQ socket, answered with a signed `handshake_reply` `{"status":"ok"}`. For those Themisto writes a registration file (`transport`, `signature_scheme`, `ip`, `key`, `registration_port`) and passes it as `--connection_file`.

## 3. Themisto's WebSocket API

Connect to `ws://127.0.0.1:<wsPort>/sessions/<sessionId>/messages?token=<token>` (see [Access](#access)). An unknown session id closes the socket with code **1008** (`unknown session`). At most one connection per session is relayed to at a time: the most recent one owns the kernel's output stream. All frames are JSON text, sent uncompressed (the server does not accept `permessage-deflate`: the client is on the same machine, and compressing a large message cost several times what sending it does).

**One WebSocket message may carry several frames**, separated by `
` (each frame is a single line of JSON; JSON text never contains a raw newline): split every message on `
`. Each connection has its own sender thread and queue (`native/src/themisto/outbox.hpp`); what has queued up while the previous send went out is sent together, up to 1 MB per message, and the sender waits while more than 1 MB is still unsent. This keeps a kernel that outpaces the client (tens of thousands of messages a second) from stopping the connection from reading the client's requests. Nothing is dropped anywhere on the output path: the IOPub sockets have no high-water mark.

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
- Requests are answered by the kernel's main thread, so they queue behind a running execution -- except `interrupt_request` and the requests a kernel answers while busy (Carpo: `complete_request`, `inspect_request`, `is_complete_request`; Callisto: `is_complete_request`), which get their reply during it. Each still gets its own `status` busy/idle pair, parented to it.
