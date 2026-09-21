# Types

Everything here is exported from `jovian` (`lib/types/`). Snake_case fields (`cursor_pos`, `execution_state`, …) are Jupyter's wire names, passed through unchanged; camelCase fields are Jovian's.

## `EngineOptions`

Passed to `SessionManager.createSession()`; also `Partial<EngineOptions>` to `Session.restart()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `kernelType` | `'r'` \| `'python'` | `'r'` | Which kernel the session runs. Selects which of the fields below apply and which executable (`elara` / `carpo`) is spawned. |
| `rHome` | string | discovered | R installation (`R RHOME`). When omitted it is found from `$R_HOME`, then `R RHOME` (R on PATH), then the Windows registry; pass it to pick a specific installation. |
| `rPath` | string | `<rHome>/bin/x64` (Windows) | Directory containing `R.dll`; put on the kernel's `PATH`. |
| `rLibs` | string | — | Extra library path (`R_LIBS`, `R_LIBS_USER`, and `R_LIBS_SITE` on Windows); where `hera` is looked up and installed. |
| `pandocPath` | string | — | Directory with a `pandoc` binary, for bundled R installs that do not ship it on `PATH` (`RSTUDIO_PANDOC`). |
| `heraSrcPath` | string | — | Source directory of the `hera` package. If set, Elara installs it (`remotes::install_local`, needs `remotes`) when it is missing or older than the source. **There is no built-in default**: if `hera` is not installed and this is unset, R code cannot run (see [Troubleshooting](../troubleshooting.md#hera-is-not-installed)). |
| `pythonHome` | string | discovered | Python installation prefix (`PYTHONHOME`). Its shared library is loaded from here. When omitted it is found from `$PYTHONHOME`, then `python3` / `python` on PATH (`sys.base_prefix`). |
| `pythonPath` | string | — | Extra `PYTHONPATH`. |
| `venvPath` | string | — | A venv whose `site-packages` is added to `sys.path`. `pythonHome` must still point at the *base* install. |
| `workingDirectory` | string | supervisor's cwd | Directory the kernel process starts in (`getwd()` / `os.getcwd()`); relative paths resolve against it. Must exist, or `createSession()` rejects with `workingDirectory does not exist or is not a directory: <path>`. Kept across `restart()`. |
| `queueSize` | number | 100 | Max queued `execute()` calls. |
| `enableLogging` | boolean | false | Log the first 100 characters of every raw message frame. |
| `enableMetrics` | boolean | false | Print a messages/second line per message. |
| `logger` | `(level, message, data?) => void` | console | Receives the library's logs (`trace` … `error`). |

## Execution

### `ExecutionOptions`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `timeout` | number (ms) | 30 000 | `0` = none. Cleared while the kernel waits for input. When it fires the call rejects and, unless `interruptOnTimeout` is `false`, the kernel is interrupted too. |
| `interruptOnTimeout` | boolean | true | On a timeout, also send the kernel an interrupt (the rejection message then ends `(the kernel was interrupted)`). Set `false` to leave it running. |
| `allowStdin` | boolean | false | Permit `input()` / `readline()`. |
| `stopOnError` | boolean | false | On failure, skip everything queued behind this call (they resolve `aborted`). Also forwarded to the kernel as `stop_on_error`. |
| `userExpressions` | `Record<string, string>` | — | Expressions evaluated after the code, if it succeeded. |
| `silent` | boolean | false | No `execute_input`, no execution count, no history. |
| `storeHistory` | boolean | true | Record in the kernel's history (ignored when `silent`). |

### `ExecutionResult`

| Field | Type | Meaning |
|---|---|---|
| `success` | boolean | `true` iff the kernel's reply status was `ok`. |
| `output` | `JupyterMessage[]` | Every `stream` / `execute_result` / `display_data` / `update_display_data` / `clear_output` / `error` message the execution produced. |
| `error` | `Error?` | For a failed execution: the message; for an aborted one: `Execution aborted: an earlier execution failed with stopOnError`. |
| `executionCount` | number? | From the reply; absent for a silent execution. |
| `status` | `'ok'` \| `'error'` \| `'aborted'`? | The reply's own status. |
| `aborted` | `true`? | Present when skipped because of an earlier `stopOnError` failure, or when the kernel replied `aborted`. |
| `userExpressions` | `Record<string, UserExpressionResult>`? | Present when `userExpressions` were requested and evaluated. |

```typescript
type UserExpressionResult =
    | { status: 'ok'; data: Record<string, any>; metadata: Record<string, any> }
    | { status: 'error'; ename: string; evalue: string; traceback: string[] };
```

### `ExecutionHistoryEntry`

`{ code: string; executionCount?: number; time: number; messages: JupyterMessage[]; truncated?: boolean }` — one entry of `Session.getHistory()`. `truncated` is `true` when the execution printed so much that the oldest stream (stdout/stderr) messages were dropped: an entry keeps the newest ~500 000 characters of stream text and everything else in full.

### `ShinyAppOptions` / `ShinyAppHandle`

`ShinyAppOptions`: `appDir` (required), `port` (default: a free port), `host` (`'127.0.0.1'`), `launchBrowser` (false), `readyTimeout` (10 000 ms), `env` (`Record<string,string>`, applied with `Sys.setenv()` before launch). `ShinyAppHandle`: `{ host, port, url, done: Promise<ExecutionResult> }` — `done` resolves when the app stops.

## Session status

Returned by `Session.status()`.

### `SessionStatusInfo`

`{ sessionId: string; status: 'starting' | 'ready' | 'stopped' | 'crashed'; kernelType: 'r' | 'python'; workingDirectory: string; pid: number; memoryBytes: number | null; heartbeat: HeartbeatInfo | null }` — `pid` is `0` when the kernel process is not running; `memoryBytes` is `null` where it can't be read; `workingDirectory` is empty when the session was created without one.

### `HeartbeatInfo`

`{ hasPong: boolean; rttMs: number; sinceLastPongMs: number; misses: number }` — the supervisor's view of the kernel's heartbeat channel. `hasPong` is `false` until the first ping is answered (`rttMs` and `sinceLastPongMs` are then meaningless); `rttMs` is the round trip of the last answered ping in ms; `sinceLastPongMs` how long ago it arrived; `misses` the number of consecutive unanswered pings. Kept fresh by a kernel thread that is independent of the one running code, so it keeps answering while a cell runs.

## Protocol reply contents

| Type | Fields |
|---|---|
| `CompleteReplyContent` | `status`, `matches: string[]`, `cursor_start`, `cursor_end`, `metadata` |
| `InspectReplyContent` | `status`, `found: boolean`, `data`, `metadata` |
| `IsCompleteReplyContent` | `status: 'complete' \| 'incomplete' \| 'invalid' \| 'unknown'`, `indent?` |
| `KernelInfoReplyContent` | `status`, `protocol_version`, `implementation`, `implementation_version`, `language_info: { name, version, mimetype, file_extension, pygments_lexer?, codemirror_mode?, nbconvert_exporter? }`, `banner`, `help_links?` |
| `CommInfoReplyContent` | `status`, `comms: Record<string, { target_name: string }>` |
| `InterruptReplyContent` | `status` |
| `ShutdownReplyContent` | `status`, `restart: boolean` — the payload of the `'shutdown_reply'` event |

### History

```typescript
interface KernelHistoryOptions {
    histAccessType?: 'tail' | 'range' | 'search';   // default 'tail'
    output?: boolean;                               // default false (the kernel records no output today)
    raw?: boolean;                                  // default true
    n?: number;                                     // 'tail'/'search'; default 100
    session?: number; start?: number; stop?: number;    // 'range'
    pattern?: string; unique?: boolean;                 // 'search' (glob: * ?)
}
type KernelHistoryEntry = [session: number, lineNumber: number, input: string | [input: string, output: string]];
```

## Messages

```typescript
interface JupyterMessage<T = any> {
    topic: string;          // iopub: 'kernel_core.<kernel id>.<msg_type>'; shell/control: the msg_type
    msgType: string;
    channel: 'iopub' | 'shell' | 'stdin' | 'control';
    parentMsgId: string;    // the id of the request that caused it ('' for unsolicited messages)
    content: T;
    timestamp: number;      // client receive time, ms
    raw: string;            // the raw frame text
}
```

Typed `content` interfaces: `StreamContent` (`name: 'stdout'|'stderr'`, `text`), `ExecuteResultContent`, `DisplayDataContent`, `UpdateDisplayDataContent` (`transient?: { display_id? }`), `ClearOutputContent` (`wait`), `ErrorContent` (`ename`, `evalue`, `traceback`), `StatusContent` (`execution_state: 'busy' | 'idle' | 'starting'`), `ExecuteInputContent` (`code`, `execution_count`), `InputRequestContent` (`prompt`, `password`), `CommOpenContent` / `CommMsgContent` / `CommCloseContent`.

`ExecutionState` = `'busy' | 'idle' | 'starting'`. `LogLevel` = `'trace' | 'debug' | 'info' | 'warn' | 'error'`.
