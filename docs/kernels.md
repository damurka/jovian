# The kernels: Elara (R) and Carpo (Python)

Both kernels are the same program shape — `main()` parses a few flags, sets up environment variables, loads the language runtime dynamically, starts an Adrastea `Kernel` with a language-specific `Interpreter`, and runs its request loop on the main thread — and differ in how the interpreter turns requests into language calls. This page covers what a *user* of a session can observe and where the two differ. For the shared machinery see [Architecture](architecture/overview.md); for adding a third kernel see [C++ usage](cpp-usage.md).

## Launch modes

| Mode | Command line | Used by |
|---|---|---|
| **Supervised** | `--registration-ip <ip> --registration-port <port> --key <key>` plus the environment flags below | Themisto (what `SessionManager` uses) — the kernel binds its ZMQ ports and reports them to the supervisor's registration socket. |
| **Connection file** | `-f <connection_file>` / `--connection-file <file>` | A Jupyter frontend (`jupyter lab`, `jupyter console`) via a kernelspec: `npm run jupyter:kernelspec` writes `kernelspec/elara/kernel.json` and `kernelspec/carpo/kernel.json` (`interrupt_mode: "message"`); install with `jupyter kernelspec install <dir> --user --name elara`. The paths to R/Python are baked into the file at generation time. Elara's connection-file mode has been exercised; Carpo's spec is generated but has not been tried against a real Jupyter install. |

Environment flags — Elara: `--r-home`, `--r-path`, `--r-libs`, `--pandoc-path`, `--hera-src-path`. Carpo: `--python-home`, `--python-path`, `--venv-path`. The kernel translates them into environment variables (`R_HOME`, `PATH`, `R_LIBS`/`R_LIBS_USER`, `RSTUDIO_PANDOC`, `ELARA_HERA_SRC`; `PYTHONHOME`, `PYTHONPATH`, `CARPO_VENV_PATH`) before loading the runtime. `workingDirectory` is not a kernel flag: Themisto starts the kernel *process* in that directory (`chdir` between `fork` and `exec` on POSIX, `lpCurrentDirectory` on Windows) after checking it exists.

Kernel stdout/stderr (start-up notes such as `[R Interpreter] .libPaths() …`) is captured by Themisto and re-printed on its own stderr, prefixed `[elara]` / `[carpo]`.

## Elara (R)

### Start-up

1. `R_HOME`, `PATH` (R's `bin` directory) and `R_LIBS*` are set; R's shared library is loaded (`R.dll` / `libR.so` / `libR.dylib` — see [Architecture](architecture/overview.md#dynamic-loading-of-r-and-python)).
2. R is started. On Windows (R ≥ 4.2) with the documented `Rstart` embedding sequence and an Elara `ReadConsole` callback, so `readline()` works over the stdin channel and R is marked interactive; on older R via plain `Rf_initEmbeddedR` (no `readline()` support). Elara also calls `GA_initapp` on Windows, without which the first `plot()` crashes the process. On Linux/macOS it sets `R_Interactive` for the same `readline()` reason.
3. The locale is switched to UTF-8 on Windows.
4. `hera` is loaded — installed first from `heraSrcPath` if missing or stale (see [Environments](guides/environments.md#the-hera-package-required)). If it cannot be loaded the kernel still starts and only logs a warning.

### Executing code (`hera`)

`RInterpreter::executeRequestImpl` calls `hera:::hera_call("execute", code, count, silent)`, which:

- parses the code (a parse failure is an `error` with `ename` `PARSE ERROR`);
- runs it with `evaluate::evaluate(…, stop_on_error = 1)` — evaluation stops at the first error in a cell;
- **stdout** → `stream` `stdout`; **messages** and **warnings** → `stream` `stderr` (warnings formatted as `Warning message in <call>:` followed by the text); **errors** → an `error` message whose `traceback` is a coloured (cli) trace, and the failed `execute_reply`;
- **plots** are drawn to a null device and, per cell, sent as one `display_data` with `text/plain` and `image/png` (`options(jupyter.plot_mimetypes)`; `ggplot` objects are printed);
- the **visible value** becomes an `execute_result` whose `data` is a mime bundle from `IRdisplay::prepare_mimebundle` — `text/plain` normally, plus `text/html` for htmlwidgets, Shiny tags and R help pages;
- `user_expressions` are evaluated afterwards with `eval(parse(text = expr))` + `print()` capture in the global environment; each fails independently.

Code runs in the **global environment**. R output that goes through R's console writer is streamed as it happens (`WriteConsoleEx` hook), not batched — subject to the ~50 ms / 16 KB coalescing every kernel applies (see [Protocol](protocol.md#1-jupyter-messages-the-kernels-support)).

**Live output inside one long expression.** `evaluate` captures stdout into a temporary file and normally hands it over only when a top-level expression ends (or a message/warning/plot happens), so `for (i in 1:1e6) print(i)` would show nothing until it was over. `hera` (`patch_evaluate_sink()` in `packages/hera/R/execute.R`) wraps the one internal `evaluate` function that creates that sink so it is a *split* sink (`sink(con, split = TRUE)`): the same output is also teed to R's console, and the console hook publishes it immediately; `try()` output is pointed at the same place, and `evaluate`'s own stdout handler is then silenced so nothing arrives twice. A silent execution is not teed. This needs **`hera` >= 0.6.0.9001**; with an older installed `hera` everything still works but such output arrives when the expression ends. Update it with `npm run hera:install`.

Rich output from R code: `hera::display_data(list("text/html" = "<b>hi</b>"))` publishes a `display_data`; `hera::clear_output(wait = FALSE)` publishes `clear_output`; `hera:::update_display_data()` publishes `update_display_data` (not exported).

### Completion, inspection, is_complete

- `complete_request` → `hera::complete()` (uses `utils:::.completeToken`); `matches` are R names (`print`, not `print(`).
- `inspect_request` → the token at the cursor is evaluated; for a function its help page (HTML + text) is returned, otherwise sections *Class attribute*, *Printed form*, *Help document*.
- `is_complete_request` → `R_ParseVector` status: `complete` / `incomplete` / `invalid`.

### Comms

The full comm API is available through `hera` (`CommManager`, `Comm`) — see [Comms](guides/comms.md). `hera` depends on `jsonlite`, `R6`, `glue` and `cli`.

### Interrupt

Sets R's user-break flag (`R_interrupts_pending` on POSIX, `UserBreak` on Windows) from the control-watcher thread; R unwinds at its next interrupt check. `kernel_info` reports `language_info.name` `R` and R's `major.minor` version (`implementation` is reported as `xr`).

### Shiny

`session.createShiny({ appDir })` runs `shiny::runApp()` in the session. It blocks the kernel for as long as the app runs (so nothing else can execute in that session), and the supervisor's stop path force-kills it because the interpreter never returns to process `shutdown_request`. Use `interrupt()` to stop the app without killing the session.

## Carpo (Python)

### Start-up

`PYTHONHOME` / `PYTHONPATH` / `CARPO_VENV_PATH` are set, `libpython` is located and loaded (see [Architecture](architecture/overview.md#dynamic-loading-of-r-and-python)) and `Py_Initialize()` runs on the kernel's main thread. Then a **bootstrap module** — Python source embedded in `interpreter_py.cpp` (`kBootstrapSource`) — is executed once in its own private namespace, so none of its helpers appear in the user's `globals()`. It defines `__carpo_run`, `__carpo_is_complete`, `__carpo_complete`, `__carpo_inspect`, `__carpo_eval_expr`, activates the venv's `site-packages`, replaces `builtins.input`, and installs `signal.default_int_handler` for SIGINT (see [Interrupting](guides/interrupting.md)).

### Executing code

`PyInterpreter::executeRequestImpl` calls `__carpo_run(code, __main__.__dict__)`:

- the code is parsed with `ast`; if the last statement is a bare expression it is evaluated separately and, if the value is not `None`, published as an `execute_result` with `text/plain` = `repr(value)` — exactly what a REPL shows;
- `sys.stdout` / `sys.stderr` are redirected to a stream that calls a native callback on every `write()`, so output streams **as it is written**, not at the end (published through the same ~50 ms / 16 KB coalescing as R, so a flood of `print()` calls becomes far fewer messages);
- any exception is caught (`BaseException`, so `KeyboardInterrupt` too) and returned as `ename` (type name), `evalue` (`str(e)`) and `traceback` (formatted lines);
- `user_expressions` are `eval`'d in the same globals and `repr`'d.

Code runs in the `__main__` namespace and state persists across executions and across `execute()` calls of the same session.

### Completion, inspection, is_complete

- `complete` → `rlcompleter` on the token before the cursor; matches include the call paren for functions (`print(`).
- `inspect` → `eval`s the token: signature (when available), `Type: <name>`, then the docstring or `repr`. `text/plain` only.
- `is_complete` → `codeop.compile_command`.

### `input()`

`builtins.input` is replaced with a function that goes through the stdin channel. Without `allow_stdin` it raises `RuntimeError` with the message quoted in [Interactive input](guides/interactive-input.md).

### venvs

`pythonHome` is the **base** installation; `venvPath` adds that venv's `site-packages` to `sys.path`. See [Environments](guides/environments.md#python).

### Interrupt

A real SIGINT delivered to the interpreter thread; Python raises `KeyboardInterrupt` (also waking a blocked `time.sleep()`), which `__carpo_run` returns as an ordinary `error`.

## Differences at a glance

| | Elara (R) | Carpo (Python) |
|---|---|---|
| Runtime dependency | R (+ the `hera` R package, + CRAN deps) | CPython 3 with its shared library |
| Global scope | `.GlobalEnv` | `__main__` |
| Rich output | `display_data`, plots (`image/png`), `text/html`, `update_display_data`, `clear_output` (via `hera`) | `execute_result` `text/plain` only — no `display_data` / plots yet |
| stderr stream | messages and warnings | `sys.stderr` |
| Completions | R names | `rlcompleter` (with `(` for callables) |
| Inspect | help pages (HTML + text) | signature / docstring (text) |
| Comms | yes (`hera::CommManager`) | no targets can be registered |
| Interrupt | user-break flag | SIGINT → `KeyboardInterrupt` |
| `kernel_info` | `R`, `implementation: "xr"` | `python`, `implementation: "carpo"`, banner `carpo (Python x.y.z)` |
| Extra environments | `rLibs` | `venvPath`, `pythonPath` |

## Known limits (both kernels)

- **One thing at a time.** A kernel is single-threaded: requests other than `interrupt_request` wait for a running execution. Use separate sessions for concurrency.
- **Interrupts cannot break native code** that never returns to the interpreter, nor a read blocked on `input()`.
- **History is in-memory**, holds inputs only, and is lost on restart.
- **No debugger protocol**, no `input_reply` password prompts (`password` is always `false`).
- **Shiny / long calls block the session**; the supervisor's stop path force-kills such a kernel after ~2 s.
- **`inspect_request.detail_level`** is accepted but ignored.
