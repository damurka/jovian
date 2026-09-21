# jovian playground

A browser UI for driving **real** R and Python kernel sessions through jovian's public `Session` API — every session is an actual `elara` / `carpo` process spawned by `themisto`, not a mock. Built with Next.js (App Router), React and TypeScript.

## Run it

Prerequisites: the repo's native binaries and library are built (`npm run build` in the repo root — see the main README's *Requirements*), and R and/or Python are installed.

```sh
# from the repo root
npm run playground:install   # once: installs this app's own dependencies
npm run playground           # dev server  -> http://127.0.0.1:4173

npm run playground:build     # production build
npm run playground:start     # serve it
```

The playground has its own `package.json` / `node_modules`; the repo's CI never installs it.

| Variable | Effect |
|---|---|
| `PLAYGROUND_PORT` | Port to listen on (default `4173`). The server binds to `127.0.0.1` only — it executes arbitrary code in real kernels, so it must not be reachable from other machines. |
| `R_HOME`, `R_PATH`, `R_LIBS` | Override the detected R installation. |
| `PYTHONHOME`, `PYTHONPATH`, `VIRTUAL_ENV` | Override the detected Python installation. |
| `JOVIAN_NATIVE_DIR` | Directory holding `themisto`, `elara` and `carpo` (default `<repo>/dist/native/Release`). Handy for running against a copy while the originals are being rebuilt — Windows will not let you overwrite a running executable. |
| `JOVIAN_DIST_DIR` | Where the built jovian library lives (default `<repo>/dist/lib`). |

R and Python are found the same way on every platform (`lib/env.mjs`): the environment variable, then `R RHOME` / `python3 -c ...`, then the Windows registry and the usual install locations (Program Files, `/Library/Frameworks/R.framework`, Homebrew, `/opt/R/<version>`, `/usr/lib/R`, …). The new-session dialog is pre-filled with whatever was found.

## What it does

- Sessions sidebar (R and Python side by side) with live PID and memory, kernel version (from a real `kernel_info_request`), and the working directory each kernel started in.
- Run code (`Shift+Enter`), interactive `input()` / `readline()`, plots, streaming output, errors with tracebacks, `clear_output` / `update_display_data`.
- **Restart / Stop / Interrupt / Remove** — the lifecycle controls call the real protocol paths. There is no separate Kill: Stop already shuts the kernel down gracefully and force-kills it if it does not exit, and it also releases a crashed session.
- **Heartbeat readout** in the KERNEL LIFECYCLE header (`HEARTBEAT 0.7ms`): the round trip of the kernel's heartbeat ping, polled every second together with PID and memory (`/api/sessions/:id/info`, the supervisor's session JSON). Green under 100 ms, amber under 1 s, red for no reply or a crashed kernel, grey when idle/unknown. The kernel answers from its own thread, so it stays live while a cell runs.
- **Shortcuts:** `Shift+Enter` run, `Tab` complete now, `Shift+Tab` / `Ctrl+I` inspect, `Ctrl+L` clear output, `Esc` interrupt a running cell (it closes an open completion list first). An interrupted cell ends with "Execution interrupted".
- A refresh rebuilds everything from the server: sessions, names, transcripts (`Session.getHistory()`).
- **Completion and inspect**, both real Jupyter requests:
  - **As you type**, completions appear on their own: about 180 ms after you stop typing, when the caret is at the end of an identifier of two or more characters, or right after `$`, `@`, `::` or `.` (`df$`, `stats::`, `os.`). The list never edits your text by itself: `Tab` accepts the highlighted item, `↑`/`↓` move, `Enter` accepts only once you have arrowed into the list (otherwise it stays a newline), `Esc` closes. Deleting does not trigger it, and it is never queued behind a running cell.
  - `Tab` asks for completions right away (common prefix first, then the same picker). At the start of a line `Tab` indents.
  - `Shift+Tab` (or `Ctrl+I`) inspects the symbol at the caret — including right after `(`, so `mean(` shows help for `mean`.
  - **Resting the mouse** for about 450 ms on a word in the input box or in a cell's echoed code inspects it; so does **resting the caret** on a word after you move it (arrow keys, `Home`/`End`, a click). These popovers close when the pointer leaves the word (unless you moved onto the popover to read it), and any key press dismisses a pending one.
  - In the output window, **double-click** a word to inspect it, or drag-select an expression and use the *Inspect* chip.
  - The automatic paths (as-you-type completion, hover and caret-rest inspect) skip a busy kernel silently: their requests carry `noWait`, the server answers "busy" at once, and nothing is shown. The explicit ones (`Tab`, `Shift+Tab`, double-click) wait instead. A kernel that is running code cannot answer either until it finishes. The server does not fail them: the request waits for the kernel (up to 30 minutes; an idle kernel is given 2 s), the input shows "Kernel is running code — completing when it finishes…", the inspect card says the answer will appear as soon as the kernel finishes (or press Interrupt), and a completion result is dropped if you have typed on meanwhile.
- **No execution timeout by default** — a cell runs until it finishes or you press Interrupt (the execute route passes `timeout: 0` unless a request asks for one). A client-side timeout would leave the kernel busy with code nobody is waiting for; the library also interrupts a timed-out execution by default (`interruptOnTimeout`).
- **Output is capped per block** (`MAX_OUTPUT_CHARS` = 200 000 in `lib/transcript.ts`): a cell that prints millions of lines keeps the last 200 000 characters and shows "earlier output not shown". `Session.getHistory()` (used to rebuild the transcript after a refresh) bounds stream text per execution too.
- Live output from one long R expression needs `hera` >= 0.6.0.9001 (`npm run hera:install` from the repo root); older versions show it when the expression ends.

## Layout

```
app/                 Next.js App Router
  api/…              route handlers (sessions, execute, complete, inspect, stream (SSE), …)
components/          React components (Playground, Sidebar, ConsoleView, InputDock, …)
lib/
  env.mjs            cross-platform R / Python discovery
  transcript.ts      the console transcript as pure functions over plain data
  client/store.ts    the whole client state as one pure reducer
  server/registry.ts the server-side session registry (a globalThis singleton)
proxy.ts             rejects cross-origin state-changing API requests (CSRF guard)
test/                node:test unit tests for transcript.ts and store.ts
```

The server loads the built library (`dist/lib`) with a runtime import, so it is exactly the module the integration tests use and is not bundled. The session registry lives on `globalThis` so hot reloads in `next dev` do not orphan running kernels.

## Tests

```sh
npm --prefix tools/playground test        # transcript + store reducers (no browser needed)
npm --prefix tools/playground run typecheck
```
