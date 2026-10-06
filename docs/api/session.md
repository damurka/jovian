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

### `r.createShiny(options: ShinyAppOptions): Promise<ShinyAppHandle>`

R only. Runs `shiny::runApp()` in the session (with `timeout: 0`) and resolves once the port accepts connections; the returned `done` promise resolves when the app stops. The session is busy for as long as the app runs.

## Protocol requests

Each is a real Jupyter request answered by the kernel; each rejects on an `error`/`aborted` status, on timeout (10 s), or if the session ends. They are queued **behind a running execution** on the kernel's main thread, except: `interrupt()`; for Python, `complete()`, `inspect()` and `isComplete()`, which the kernel answers mid-run; for Stata, `isComplete()`; and for R, `complete()` and `inspect()` of package functions, which a helper R process answers while the session is busy (see [Kernels](../kernels.md#completion-inspection-is_complete); the reply's `metadata['jovian/answered-by']` is `'helper'`).

| Method | Sends | Resolves with |
|---|---|---|
| `complete(code, cursorPos = code.length, { waitForCell?, timeout? })` | `complete_request` | `{ status, matches: string[], cursor_start, cursor_end, metadata }` |
| `inspect(code, cursorPos = code.length, detailLevel = 0, { waitForCell?, timeout? })` | `inspect_request` | `{ status, found, data: { 'text/plain'?, 'text/html'? }, metadata }` |
| `isComplete(code)` | `is_complete_request` | `{ status: 'complete' \| 'incomplete' \| 'invalid' \| 'unknown', indent? }` |
| `kernelInfo()` | `kernel_info_request` | `{ protocol_version, implementation, implementation_version, language_info: { name, version, … }, banner, … }` |
| `commInfo(targetName?)` | `comm_info_request` | `{ status, comms: { [commId]: { target_name } } }` |
| `queryKernelHistory(options?)` | `history_request` | `KernelHistoryEntry[]` — see [History](../guides/history.md) |
| `request<T>(msgType, content?, { timeout? })` | any whitelisted request/reply pair | the reply `content` |

`waitForCell: false` (for as-you-type requests): when a cell is running and the answer would have to wait for it, resolve at once with nothing found and `metadata['jovian/busy'] = true` instead. `timeout` (ms, default 10 000) is how long to wait for the reply.

`request()` is the generic form behind the methods above; it always sends the request to the session's own kernel (no helper). It accepts the request types in the [whitelist](../protocol.md#client--themisto); the reply type is derived by replacing `_request` with `_reply`. It is not for `execute_request`, `input_reply` or `shutdown_request`.

## What only one kernel can do: `session.r`, `session.stata`

An R session (Elara) has `session.r`, an `RSession` with its packages, R's help and Shiny; a Stata session (Callisto) has `session.stata`, a `StataSession` with the dataset in its memory. For any other kernel they are `undefined`, so the type says what the session can do. The methods used to be the session's own (`session.listPackages()`, `session.stataDataset()`, ...): those names remain for now, deprecated, and reject with `…: only for R sessions (Elara)` or `…: only for Stata sessions (Callisto)` for another kernel.

## R packages (`session.r`)

R sessions (Elara) only: answered by the kernel's own R code (`.jv.rpc.*` in `packages/hera/R/packages.R`, called through `rpc.R`, as Ark's `.ps.rpc.pkg_*`) -- except `installPackages()`, which goes through the session manager's installer (`manager.ensureRPackage()`, see [Installing R packages](../guides/environments.md)). A session loads no package of its own besides R's, so any package can be installed, updated or removed — unless the user's code has loaded it (on Windows a loaded package's DLL cannot be replaced). Repositories: `options.repos` first, then the session's own (`getOption("repos")`, CRAN's cloud mirror when unset); for `installPackages()`, then CRAN.

| Method | Description |
|---|---|
| `r.listPackages()` | `RPackageInfo[]`: every installed package — `name`, `version`, `library`, `priority` (`base`/`recommended`/`''`), `loaded`, `attached`. |
| `r.packagesInstalled(packages, minVersions?)` | `RPackageCheck[]`: `{ name, version (null if absent), installed }`, `installed` meaning at least `minVersions[name]`. |
| `r.outdatedPackages(options?)` | `RPackageUpdate[]`: installed packages with a strictly newer version in the repositories (`installed`, `available`, `library`, `repository`). |
| `r.searchPackages(query, options?)` | `RPackageSearchResult[]`: packages whose name matches `query`, an exact match first (`limit`, default 100). |
| `r.installPackages(packages, options?)` | `RPackageInstallResult`: `{ installed: [{ name, version }], failed, warnings }` (`warnings`: why each failed). Each package is installed, or updated to the newest in the repositories, with what it needs, by `manager.ensureRPackage()` in a packages session of the manager's, not in this session; its progress arrives as the session's `stdout` events. `options.lib` chooses the library (default: the session's first). Default timeout 30 minutes. |
| `r.removePackages(packages, options?)` | The packages removed. |
| `loadedRPackages()` (also `r.loadedPackages()`) | `string[] \| undefined`: the packages the session has loaded now (`loadedNamespaces()`; their DLLs are in use). `undefined` when that can't be known: the kernel is running code (it may load anything), or the kernel is not Elara. Answers at once and never interrupts a running cell. The installer asks this before replacing packages. |

The quick ones are answered as a user expression of a silent execution (no output, no execution count).

## Variables and tables (R, Python)

What a variables pane and a data viewer show, for R sessions (Elara: hera's `.jv.rpc.var_list` / `var_table`, `packages/hera/R/variables.R`) and Python sessions (Carpo answers the user expressions `.jovian_variables` / `.jovian_table` itself). Each is a user expression of a silent execution, so it waits for a running cell. Stata sessions have a dataset instead (below).

| Method | Description |
|---|---|
| `listVariables()` | `SessionVariable[]`: the objects of R's global environment or Python's `__main__` (Python: not modules, not names starting with `_`; at most 5000), sorted by name -- `{ name, type, size, summary, table }`: R's class or Python's type name, `"3 × 11"` / `"10"` / `""`, a one-line preview (`3 obs. of 11 variables`, `1.5 NA`, `f(a, b=2)`), and whether `readTable()` reads it. |
| `readTable(name, options?)` | `TablePage`: `{ name, rowCount, columns: [{ name, type }], start, count, rowLabels, rows }` -- `count` rows (default 100, at most 100 000) from `start` (default 1) of a table: an R data frame (tibble, data.table) or matrix; a pandas DataFrame or Series, a numpy array of 1 or 2 dimensions, a polars DataFrame. Values are text as the language prints them (R: factor levels, dates, `NA`; Python: `1.0`, `NaN`, `None`); `rowLabels` are R's row names or a pandas index when they are more than the row numbers, else `null`. Anything else is an error. |

```ts
const variables = await session.listVariables();
const page = await session.readTable('cars', { start: 1, count: 50 });
```

## The Stata dataset (`session.stata`)

Stata sessions (Callisto) only: the dataset in memory, read by the kernel itself (its Mata library and its plugin, see [Kernels](../kernels.md#the-dataset)) -- what a variables pane or a data viewer shows. Each is a user expression of a silent execution, so it waits for a running cell.

| Method | Description |
|---|---|
| `stata.dataset()` | `StataDataset`: `{ frame, observations, filename, changed, variables: [{ name, type, format, label, valueLabel }], valueLabels: { name: { values, labels } } }` (value labels' first 1000 values). |
| `stata.data(options?)` | `StataDataPage`: `{ start, count, observations, variables, formatted, rows }` -- `count` observations (default 100, at most 100 000) from `start` (default 1) of `variables` (default all), `rows[i][j]` being variable `variables[j]` of observation `start + i`. Raw values are numbers, strings, `null` for the missing value `.` and, in a numeric variable, `".a"` to `".z"` for the extended ones; with `formatted: true` they are strings as Stata's Data Editor shows them (value labels, display formats: `4,099`, `02jan2020`, `Domestic`). A variable that does not exist is an error. |

```ts
const data = await session.stata.dataset();
const page = await session.stata.data({ start: 1, count: 50, variables: ['make', 'price'], formatted: true });
```

## The host's UI (`'ui'` event)

R code using **rstudioapi** asks the application running the session — as Ark's `tools:rstudio` asks Positron. The R kernel provides the RStudio API (`.rs.api.*` in `tools:rstudio`, `packages/hera/R/ui.R`) and makes `rstudioapi::isAvailable()` say `TRUE` (`.Platform$GUI` is left alone). Each call is a `'ui'` event, `{ method, params, reply? }` (`UiRequest`):

- **notifications** (no `reply`): `viewer` `{ url, height }`, `navigateToFile` `{ file, line, column }`, `documentNew`, `insertText` `{ ranges, text, id }` (ranges `{ start: { line, character }, end }` from 0), `setSelectionRanges`, `executeCommand`, `sendToConsole`, `restartSession`, `openProject`, `previewRd`;
- **questions** (call `reply(answer)`; the cell waits for it): `showPrompt` → string, `showQuestion` → boolean, `showDialog`, `askForPassword` → string (a password input), `getActiveDocumentContext` / `getSourceEditorContext` → `{ id, path, contents: string[], selections: [{ start, end, text }] }`, `documentSave`, `documentSaveAll`, `getActiveProject`, `readPreference` `{ name }`.

A question reaches the host as an `input_request` carrying `jovian_ui`, whether or not the execution has `allowStdin` (the Session always answers it: a Shiny app's `createShiny()`, which allows no input, can ask too); with no `'ui'` listener it is answered at once with no answer, which gives the R function its default (`NULL` for a prompt, `FALSE` for a question). A listener must call `reply` for every question: the R code waits for it.

R code can make its own requests to the application with **`.elara.host_notify(method, params)`** (a notification) and **`.elara.host_ask(method, params, default = NULL)`** (a question, returning the answer read from JSON): the same `'ui'` events, with the application's own method names -- DataSuite's apps use `datasuite.print`, `datasuite.openChat` and `datasuite.installPackages` (datasuite.ui's `ds_host_request()`). `JOVIAN_HOST_VERSION` / `JOVIAN_HOST_MODE` set what `rstudioapi::getVersion()` / `getMode()` report (default `2025.1.0`, `desktop`).

## R help (`session.r`)

| Method | Description |
|---|---|
| `r.helpServer()` | `{ port, url }` of R's own help server in the session (`tools::startDynamicHelp()`), started if need be. Its pages link to each other. |
| `r.helpUrl(topic, pkg?)` | The help server's address for a topic (`…/library/base/html/mean.html`), or `null`. |

The server answers while the session is idle: Elara services R's events then (`R_ProcessEvents()`, and on Unix R's input handlers), as R's own console does while it waits for input.

## Comms

A *comm* is a named message stream between the client and a target registered inside the kernel (R: `.elara.CommManager$register_comm_target(name, callback)`). Carpo has no way to register targets yet, so comms are an R feature today. See [the guide](../guides/comms.md).

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

Sends the kernel `shutdown_request{restart: false}` through the supervisor, waits for the process to exit (force-killed after 2 s when the kernel did not answer the request -- it is stuck; one that answered is ending on its own and is given up to 12 s), waits up to 250 ms for the `shutdown_reply` to be observable, closes the socket and emits `'stopped'`. Idempotent. Pending work rejects (`Queue cleared`, `Session stopped`).

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
| `'exit'` | `{ reason }` | Kernel died unexpectedly (or the supervisor connection dropped). From then on `isStopped` is `true` and `execute()` rejects at once with `The session's kernel has ended (<reason>); restart() starts a new one`, instead of waiting out its timeout. The session is left recoverable via `restart()`, which clears that. Not emitted for `stop()` / `restart()`. |
| `'restarted'` | — | `restart()` finished. |
| `'stopped'` | — | `stop()` finished. |
| `'requestError'` | `{ id, error }` | The supervisor refused a fire-and-forget request (a `comm_*` message) — e.g. session not found. |
