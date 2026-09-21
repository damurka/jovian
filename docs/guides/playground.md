# The playground

`tools/playground` is a browser UI (Next.js + React) for driving **real** R and Python sessions through jovian's public `Session` API — every session is an actual `elara` / `carpo` process spawned by `themisto`. Use it to try an installation, reproduce a kernel bug, or see how the API behaves. Its own [README](../../tools/playground/README.md) is the reference; this page is the short version.

```sh
npm run build                # once: native binaries + dist/lib (the playground uses both)
npm run playground:install   # once: the playground has its own node_modules; CI never installs it
npm run playground           # http://127.0.0.1:4173
```

## What to try

| Feature | How |
|---|---|
| New R / Python session | *New Kernel Session*: pick the kernel type, R home / Python home (pre-filled from auto-discovery), library paths, and a **working directory** (empty = the server's). |
| Run code | `Shift+Enter`. `input()` / `readline()` show an input box. R plots render as images. |
| Completion | **As you type**: about 180 ms after you pause with the caret at the end of an identifier of two or more characters (or right after `$`, `@`, `::`, `.`), a list of candidates appears without touching your text. `Tab` accepts, `↑/↓` move, `Enter` accepts only after you have arrowed into the list, `Esc` closes. `Tab` also asks explicitly (completing the common prefix first, then a picker). Uses `session.complete()`. |
| Inspect | Automatic: rest the mouse (~450 ms) on a word in the input or in a cell's echoed code, or rest the caret on a word after moving it (arrows, `Home`/`End`, click), and its documentation appears; it closes when the pointer leaves. Explicit: `Shift+Tab` (or `Ctrl+I`) shows help for the symbol at the caret (`mean(` works); double-click a word in the output window, or drag-select an expression and use the *Inspect* chip. Uses `session.inspect()`. |
| Interrupt | Stops a running cell (`session.interrupt()`); try `Sys.sleep(60)` or `import time; time.sleep(60)`. |
| Restart / Stop / Remove | The lifecycle controls call the real protocol paths (`shutdown_request` with `restart: true/false`). Stop shuts the kernel down gracefully and force-kills it if it does not exit (there is no separate Kill — it would do the same thing); it also releases a crashed session. Remove forgets the session. |
| Heartbeat | The **KERNEL LIFECYCLE** header shows `HEARTBEAT 0.7ms`: the round trip of the kernel's heartbeat ping (`session.status()`), polled every second. Green under 100 ms, amber under 1 s, red for "no reply" / slow / a crashed kernel, grey when there is nothing to measure. It stays green while a cell runs, because the kernel answers from its own thread. |
| Keyboard | `Shift+Enter` run, `Tab` complete, `Shift+Tab` inspect, `Ctrl+L` clear output, `Esc` interrupt (while a cell is running; `Esc` first closes an open completion list). An interrupted cell ends with "Execution interrupted". |
| Refresh the page | Sessions, transcripts (`session.getHistory()`), versions and working directories are rebuilt from the server. |

Cells run **without a timeout** by default — press Interrupt to stop one. The automatic completion and inspect paths never wait on a busy kernel: they send `noWait` and stay silent if it is running code. For the explicit ones, a kernel that is running code cannot answer until it finishes, so the UI says so ("Kernel is running code — completing when it finishes…") and shows the result as soon as the kernel is free, instead of failing. Output that would overwhelm the page (a cell printing millions of lines) is capped: each output block keeps its last 200 000 characters and says that earlier output is not shown.

## Configuration

Environment variables (`PLAYGROUND_PORT`, `R_HOME`, `R_PATH`, `R_LIBS`, `PYTHONHOME`, `PYTHONPATH`, `VIRTUAL_ENV`, `JOVIAN_NATIVE_DIR`, `JOVIAN_DIST_DIR`) are listed in the playground README. The server binds to `127.0.0.1` only and rejects cross-origin state-changing requests — it executes arbitrary code, so never expose it beyond localhost.

## As a reference for your own app

The playground is a compact example of everything `lib/` offers: SSE streaming of `'message'` events, per-session PID, memory and heartbeat from `session.status()`, transcript restoration, completion popups and an inspect tooltip built on `complete()`/`inspect()`, and stdin handling with `'input_request'` / `sendInputReply()`. Its server-side session registry is a `globalThis` singleton so hot reloads in `next dev` do not orphan running kernels.
