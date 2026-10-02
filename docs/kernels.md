# The kernels: Elara (R), Carpo (Python) and Callisto (Stata)

The kernels are the same program shape — `main()` parses a few flags, sets up environment variables, loads the language runtime dynamically, starts an Adrastea `Kernel` with a language-specific `Interpreter`, and runs its request loop on the main thread — and differ in how the interpreter turns requests into language calls. This page covers what a *user* of a session can observe and where they differ. For the shared machinery see [Architecture](architecture/overview.md); for adding another kernel see [C++ usage](cpp-usage.md).

## Launch modes

| Mode | Command line | Used by |
|---|---|---|
| **Supervised** | `--registration-ip <ip> --registration-port <port> --key <key>` plus the environment flags below | Themisto (what `SessionManager` uses) — the kernel binds its ZMQ ports and reports them to the supervisor's registration socket. |
| **Connection file** | `-f <connection_file>` / `--connection-file <file>` | A Jupyter frontend (`jupyter lab`, `jupyter console`) via a kernelspec: `npm run jupyter:kernelspec` writes `kernelspec/elara/kernel.json`, `kernelspec/carpo/kernel.json` and `kernelspec/callisto/kernel.json` (`interrupt_mode: "message"`); install with `jupyter kernelspec install <dir> --user --name elara`. The paths to R/Python/Stata are baked into the file at generation time. Elara's connection-file mode has been exercised; Carpo's and Callisto's specs are generated but have not been tried against a real Jupyter install. |

Environment flags — Elara: `--r-home`, `--r-path`, `--r-libs`, `--pandoc-path`. Carpo: `--python-home`, `--python-path`, `--venv-path`. Callisto: `--stata-home`, `--stata-edition`. The kernel translates them into environment variables (`R_HOME`, `PATH`, `R_LIBS`/`R_LIBS_USER`, `RSTUDIO_PANDOC`; `PYTHONHOME`, `PYTHONPATH`, `CARPO_VENV_PATH`; `STATA_HOME`, `CALLISTO_STATA_EDITION`) before loading the runtime. `workingDirectory` is not a kernel flag: Themisto starts the kernel *process* in that directory (`chdir` between `fork` and `exec` on POSIX, `lpCurrentDirectory` on Windows) after checking it exists.

Kernel stdout/stderr (start-up notes such as `[R Interpreter] .libPaths() …`) is captured by Themisto and re-printed on its own stderr, prefixed `[elara]` / `[carpo]` / `[callisto]`.

## Elara (R)

### Start-up

1. `R_HOME`, `PATH` (R's `bin` directory) and `R_LIBS*` are set; R's shared library is loaded (`R.dll` / `libR.so` / `libR.dylib` — see [Architecture](architecture/overview.md#dynamic-loading-of-r-and-python)).
2. R is started. On Windows (R ≥ 4.2) with the documented `Rstart` embedding sequence and an Elara `ReadConsole` callback, so `readline()` works over the stdin channel and R is marked interactive; on older R via plain `Rf_initEmbeddedR` (no `readline()` support). Elara also calls `GA_initapp` on Windows, without which the first `plot()` crashes the process. On Linux/macOS it sets `R_Interactive` for the same `readline()` reason.
3. The locale is switched to UTF-8 on Windows.
4. `hera`, the kernel's R code built into it, is evaluated into one locked environment attached as `tools:jovian`, every name dot-named (`.jv.*`, `.elara.*`), as Ark's `tools:positron`; `View()` is replaced in `utils`. Nothing is installed (see [Environments](guides/environments.md#the-hera-package-built-in)). If it cannot be loaded the kernel still starts and logs an `ERROR` saying why.

### Executing code (`hera`)

`RInterpreter::executeRequestImpl` calls `.jv.call("execute", code, count, silent)`, which:

- parses the code (a parse failure is an `error` with `ename` `PARSE ERROR`);
- runs its top-level expressions one by one with base R — evaluation stops at the first error in a cell;
- **stdout** → `stream` `stdout`; **messages** → `stream` `stderr` as they happen; **warnings** are R's own, as in its console and Ark: `options(warn)` applies (kept until a top-level expression ends, printed at once with `warn = 1`, ignored with `warn < 0`, errors with `warn >= 2`), reported on `stderr` in R's words (`Warning message:` / `In f() : w`, `There were 50 or more warnings …`), and `warnings()` shows them; **errors** → an `error` message whose `traceback` lists the cell's own calls, outermost first, with where they are in the cell (`f() at [3]#2`) — rlang's own report for an rlang error — and the failed `execute_reply`; afterwards `traceback()` shows the calls and `options(error)` has run, as in R;
- **plots** are drawn on one null device for the whole session, as R's console has one plot window: a later cell can add to a plot (`abline()` after `plot()`), and `par()` settings last. Each changed plot is sent once complete as a `display_data` with `text/plain` and `image/png` (`options(jupyter.plot_mimetypes)`; also `image/svg+xml`, `image/jpeg`, `application/pdf`), drawn by R's own devices; one drawn before an error is still sent. Whether anything was drawn is checked in constant time (the device's display list, through Elara), so a big plot costs later cells nothing;
- the **visible value** of the cell's last expression becomes an `execute_result` with what `print()` writes, as R's console and Ark show it — or, for a value whose `print()` draws (a ggplot, a lattice plot), that plot. R help pages come as text and HTML (`tools`' `Rd2txt`/`Rd2HTML`); HTML widgets and Shiny tags through `IRdisplay` (loaded then). A top-level `on.exit()` does nothing, as in R's console;
- `user_expressions` are evaluated afterwards with `eval(parse(text = expr))` + `print()` capture in the global environment; each fails independently.

Code runs in the **global environment**. R output that goes through R's console writer is streamed as it happens (`WriteConsoleEx` hook), not batched — subject to the ~50 ms / 16 KB coalescing every kernel applies (see [Protocol](protocol.md#1-jupyter-messages-the-kernels-support)).

**How a cell runs.** `hera` (`packages/hera/R/execute.R`) evaluates the cell's top-level expressions one by one with base R, as R's console does: what they print goes straight to R's console and so is streamed as it is written, even inside one long expression; messages and warnings are published as stderr as they happen; the first error stops the cell, with a traceback of the cell's own calls (`f() at [3]#2`); plots are drawn on a device of the cell's own and sent once each is complete. Values are shown with base R's `print()`, or through `repr` for the objects it draws its own way (data frames, matrices, widgets), loaded only then.

Rich output from R code: `.elara.display_data(list("text/html" = "<b>hi</b>"))` publishes a `display_data`; `.elara.clear_output(wait = FALSE)` publishes `clear_output`; `.elara.update_display_data()` publishes `update_display_data` (not exported).

### Completion, inspection, is_complete

- `complete_request` → `.elara.complete()` (uses `utils:::.completeToken`); `matches` are R names (`print`, not `print(`).
- `inspect_request` → the token at the cursor is evaluated; for a function its help page (HTML + text) is returned, otherwise sections *Class attribute*, *Printed form*, *Help document*.
- `is_complete_request` → `R_ParseVector` status: `complete` / `incomplete` / `invalid`.

**Names from the code itself.** R's completer knows only what exists in the session. Names defined in the code being typed but not run yet (`df2 <- …` two lines up), and names a running cell defines, are added by reading the code (`lib/session/r-static.ts`: assignments in every form, `function` arguments, `for` variables, `assign("…")`; comments and strings skipped). The same reading finds the packages a running cell attaches for the helper below.

**While a cell runs**, R cannot answer these itself (its interpreter runs one thing at a time and cannot be called from another thread). The library answers `complete()` and `inspect()` from a **helper R process** instead (`lib/session/r-helper.ts`): a second, idle R of the same installation, started once an R cell has run for a second and shared by the sessions using that R. Every R execution reports, at its end, the packages the session has attached and the names it has defined (one extra `user_expressions` entry, removed from the result). The helper attaches the same packages, plus any the running cell attaches itself with `library()`, and answers. So help for a package's function (`walk`, `median`) and completion of package functions and of the session's own names work mid-cell. Inspecting something only the busy session has, an object it created, still waits for the cell. Replies from the helper carry `metadata['jovian/answered-by'] = 'helper'`. Turn it off with `new SessionManager({ busyHelper: false })`.

### Comms

The full comm API is available through `hera` (`CommManager`, `Comm`) — see [Comms](guides/comms.md). The comms are held by Elara in C++ (`native/src/elara/r/comm_r.cpp`): a comm is its id with class `Comm`, a received message a list of class `Message`, and the R functions they call are kept from R's garbage collector while in use.

### Interrupt

Sets R's user-break flag (`R_interrupts_pending` on POSIX, `UserBreak` on Windows) from the control-watcher thread; R unwinds at its next interrupt check. `kernel_info` reports `language_info.name` `R` and R's `major.minor` version (`implementation` is reported as `xr`).

### The event loop between cells

R's console runs the [`later`](https://r-lib.github.io/later/) event loop whenever R waits for input; an embedded R never waits for input, so Elara runs it itself: every ~20 ms while the kernel is idle, `later::run_now(0)` when `later` is loaded and has something due. Callbacks from `later::later()`, promises, and servers built on httpuv (`httpuv::startServer()`, plumber, a Shiny app started without blocking) therefore run **between cells**, as at RStudio's console, and what they print goes to the latest request. An error in a callback is printed and does not affect the session. While a cell runs, callbacks wait until it finishes.

### Shiny

`session.createShiny({ appDir })` runs `shiny::runApp()` in the session. It blocks the kernel for as long as the app runs (so nothing else can execute in that session), and the supervisor's stop path force-kills it because the interpreter never returns to process `shutdown_request`. Use `interrupt()` to stop the app without killing the session.

## Carpo (Python)

### Start-up

`PYTHONHOME` / `PYTHONPATH` / `CARPO_VENV_PATH` are set, `libpython` is located and loaded (see [Architecture](architecture/overview.md#dynamic-loading-of-r-and-python)) and `Py_Initialize()` runs on the kernel's main thread, which then **releases the GIL** for good: each request takes it only while it calls into Python. Then a **bootstrap module** — Python source embedded in `interpreter_py.cpp` (`kBootstrapSource`) — is executed once in its own private namespace, so none of its helpers appear in the user's `globals()`. It defines `__carpo_run`, `__carpo_is_complete`, `__carpo_complete`, `__carpo_inspect`, `__carpo_eval_expr`, `__carpo_idle`, activates the venv's `site-packages`, replaces `sys.stdout` / `sys.stderr` and `builtins.input`, sets `sys.executable` to the real interpreter (the venv's `python` when `venvPath` is given, else the base installation's) instead of `carpo`, and installs `signal.default_int_handler` for SIGINT (see [Interrupting](guides/interrupting.md)).

### Executing code

`PyInterpreter::executeRequestImpl` calls `__carpo_run(code, __main__.__dict__)`:

- the code is parsed with `ast`; if the last statement is a bare expression it is evaluated separately and, if the value is not `None`, published as an `execute_result` with `text/plain` = `repr(value)` — exactly what a REPL shows;
- it is compiled with `PyCF_ALLOW_TOP_LEVEL_AWAIT`, so a cell may `await` at top level, as in IPython (`await asyncio.sleep(1)`; a trailing `await ...` is the cell's value); such a cell runs on the session's event loop (below);
- `sys.stdout` / `sys.stderr` are a stream that calls a native callback on every `write()`, so output streams **as it is written**, not at the end (published through the same ~50 ms / 16 KB coalescing as R, so a flood of `print()` calls becomes far fewer messages). They stay in place between cells, so a thread or task still printing after its cell has finished reaches the client as part of the latest request;
- any exception is caught (`BaseException`, so `KeyboardInterrupt` too) and returned as `ename` (type name), `evalue` (`str(e)`) and `traceback` (formatted lines);
- `user_expressions` are `eval`'d in the same globals and `repr`'d.

Code runs in the `__main__` namespace and state persists across executions and across `execute()` calls of the same session.

### Threads, asyncio and processes

- **Threads** run while the kernel is idle as well as during cells: the kernel thread holds the GIL only while it calls into Python.
- **asyncio:** the session has one event loop, the current loop in every cell. Top-level `await` cells run on it, and between cells the kernel advances it every ~20 ms (`__carpo_idle`), so a task started with `asyncio.get_event_loop().create_task(...)` keeps running after its cell. `asyncio.run()` in a cell still works (it makes its own loop).
- **Processes:** `multiprocessing`, `concurrent.futures.ProcessPoolExecutor` and `subprocess.run([sys.executable, ...])` start the real Python (see `sys.executable` above). With the `spawn` start method (the default on Windows and macOS), a worker cannot see functions defined in a cell, as in any Jupyter kernel: put them in a module, or use built-ins.

### Completion, inspection, is_complete

- `complete` → `rlcompleter` on the token before the cursor; matches include the call paren for functions (`print(`).
- `inspect` → `eval`s the token: signature (when available), `Type: <name>`, then the docstring or `repr`. `text/plain` only.
- `is_complete` → `codeop.compile_command`.

All three are **answered while a cell runs** (on another thread of the kernel, taking the GIL, which a running cell gives up every few milliseconds and whenever it sleeps or waits on I/O) instead of queuing behind it. Code that holds the GIL in one long native call delays the answer until the call returns.

### `input()`

`builtins.input` is replaced with a function that goes through the stdin channel. Without `allow_stdin` it raises `RuntimeError` with the message quoted in [Interactive input](guides/interactive-input.md).

### venvs

`pythonHome` is the **base** installation; `venvPath` adds that venv's `site-packages` to `sys.path`. See [Environments](guides/environments.md#python).

### Interrupt

A real SIGINT delivered to the interpreter thread; Python raises `KeyboardInterrupt` (also waking a blocked `time.sleep()`), which `__carpo_run` returns as an ordinary `error`.

## Callisto (Stata)

### Start-up

`STATA_HOME` (and `CALLISTO_STATA_EDITION` for `--stata-edition`) are set, and Stata's shared library is loaded from `stataHome`: the first of MP, SE, BE that is installed there, unless an edition is asked for (see [Architecture](architecture/overview.md#dynamic-loading-of-r-and-python) and [Environments](guides/environments.md#stata)). Stata 17 is the first release that ships it. `SYSDIR_STATA` is set to the same directory, as pystata does, and Stata is started with `-q` (no banner). If Stata refuses to start, most often `Cannot find license file`, the kernel exits with Stata's own message. It then runs `set more off` and, on Windows, puts `c(java_home)/bin` on `PATH` for Stata's Java-based commands (pystata does the same).

### Executing code

`StataInterpreter::executeRequestImpl` runs a cell that is one plain command (one complete line, no comment, no `#delimit`, not `exit`) as typed at Stata's prompt, as pystata runs a single line. Any other cell is written to a temporary do-file and run with `include`:

- `include`, not `do`, so the cell runs in the interactive context: a `local` defined in one cell is still defined in the next, as when typing at Stata's prompt. Being a do-file, everything a do-file allows works: `/* */` and `//` comments, `///` continuations, loops, `program define`, `#delimit ;`.
- A do-file echoes its commands (`. cmd`, `> ` continuations, the numbered lines of a loop or program) and Stata 17 and 18 cannot turn that off (`set showcommand` is Stata 19's). The echo is taken out of the output as it streams (`text::EchoFilter`), with the blank line before each command, the final prompt and the return code Stata repeats after an error, so a cell shows what its commands print, whichever way it ran. A line that cannot be echo (a row of `_dots`) goes out before it ends.
- Stata prints into its output buffer; a second thread empties it every 20 ms while the command runs and publishes the text as `stream` `stdout`, so output arrives as it is produced (with the ~50 ms / 16 KB coalescing every kernel applies). The thread is woken when the command returns, so a cell's end waits for no poll (a trivial cell takes about 2 ms). Stata's output has no separate error stream: everything is stdout.
- Output is made valid UTF-8 on the way (`text::Utf8Decoder`): a character split between two reads waits for its other bytes, and text that is not UTF-8 (string data saved by Stata 13 or older, in Latin-1) is read as Latin-1, where it used to make the kernel's JSON fail.
- A non-zero return code is an `error` whose `ename` is Stata's `r(<rc>)` (`r(111)`) and whose `evalue` is the message Stata printed above it (`variable nosuchvar not found`); the `traceback` is those two lines.
- **Graphs.** `_gr_list on` makes Stata record the graphs a cell draws; afterwards each one is exported with `graph export` to a PNG and published as one `display_data` (`image/png` plus a `text/plain` placeholder), the way pystata shows graphs inline. Silent executions skip this.
- `user_expressions` are each shown with `display <expr>`; one that fails reports its own `r(<rc>)`.

There is no `execute_result`: Stata commands print, they do not return a value.

The machinery never changes the user's `r()` results: the graph list is read between `_return hold` and `_return restore`, and completion reads names through Mata.

### The dataset

What the kernel reads from Stata -- names (variables, macros, scalars, stored results, graphs, the adopath), the dataset's description and its values -- does not come from printed output, which Stata wraps at `c(linesize)`. pystata reads these through `sfi`, whose functions live inside Stata and are open to Python only (and Java); a C++ kernel has two documented ways instead:

- **Callisto's Mata library** (`native/src/callisto/stata/stata_mata.hpp`). At start-up the kernel compiles its functions (`callisto_*`) into `lcallisto.mlib` in a folder of its own and adds that folder to the adopath, so Mata loads them again after `clear all` or `mata clear`. Each writes its answer as JSON to a file the kernel reads (`StataInterpreter::mataJson()`). Names, the dataset's description (`callisto_dataset()`: frame, size, file, variables with their type, format, label and value label, value labels) and formatted values (`callisto_rows()`, a column at a time: 10 000 rows of 10 variables in about 0.35 s) come from it.
- **Callisto's plugin** (`callisto_stata.plugin`, beside `callisto`; `native/src/callisto/plugin/`), written to Stata's plugin interface (`stplugin.h`, `native/third_party/stata/`). Stata runs inside the kernel's process, so the kernel calls it (`plugin call _callisto_plugin <variables> in <a>/<b>, <address>`) with the address of a `CallistoSink` (`native/include/callisto/plugin_sink.h`) and the plugin hands it each value: a number, which of the 27 missing values, or a string's bytes -- nothing printed or written. Raw values come from it (10 000 rows of 10 variables in about 0.18 s); without it (`CALLISTO_PLUGIN` points elsewhere, or it is missing) they come from the Mata library (about 0.55 s).

Jovian asks for the dataset with two user expressions the kernel answers itself, `.callisto_dataset` and `.callisto_data` (JSON parameters), sent with an empty, silent cell: `Session.stataDataset()` and `Session.stataData()` ([Session](api/session.md#the-stata-dataset)).

### Completion, inspection, is_complete

- `complete_request` →
  - the first word of a command (also after `quietly`, `capture`, `by ...:` and the like): commands -- Stata's built-in ones, the ado-files on the adopath (read once, again after a cell that installs or changes the adopath; the parts of other commands, `regress_estat.ado`, left out) and the programs defined in the session;
  - another bare name: variable names of the dataset in memory, then scalars;
  - after `$` or `${`: global macros; after `` ` ``: local macros;
  - inside `r(`, `e(` or `s(`: the names of the stored results (macros, scalars, matrices).
- `inspect_request` → a variable: the output of `describe` and `summarize` for it; a scalar: its value; `$name`: the global's value; a command: what `which` says (the ado-file and its version line, or built-in). Anything else is not found.
- `is_complete_request` → `incomplete` while a `{` block or a `/* */` comment is open or the last line ends in `///`, `invalid` for a `}` that closes nothing, otherwise `complete`. Braces inside strings, compound strings and comments are not counted. It never calls Stata, so it is answered while a cell runs; completion and inspection wait for the cell.

### Interrupt

`StataSO_SetBreak()` from the control-watcher thread, only while a cell runs: Stata's own Break. The cell fails with `r(1)` (`--Break--`) and the session stays usable.

### Not supported

No `input_request`: Stata's `_request()` cannot be answered over the stdin channel. No comms. `python:` blocks inside a cell need Stata's Python integration, which Callisto does not set up.

## Ark (R, Posit's kernel) -- experimental

`kernelType: 'ark'` runs [Ark](https://github.com/posit-dev/ark), the R kernel inside Positron (MIT-licensed, not shipped with Jovian), under the same supervisor instead of Elara. The `ark` executable comes from `arkPath`, else `$ARK_PATH`, the copy bundled with Positron, or `ark` on `PATH`; R comes from `rHome` (set as `R_HOME`). Themisto starts it the standard Jupyter way: it writes a JEP 66 registration file and passes `--connection_file <file> --session-mode notebook`, and Ark registers with a signed `handshake_request` (see [Protocol](protocol.md#kernel-registration-jep-66)).

Verified on Windows with Ark 0.1.252 and R 4.6: start-up (about 0.3 s), output, results, errors, plots (`image/png`), `kernel_info` (`implementation: "ark"`), interrupt and restart. Not available through Jovian:

- **`complete_request` / `inspect_request` return nothing.** Ark answers these through its language server (LSP) over a Positron-specific comm, not the Jupyter requests. Jovian does not speak LSP.
- An interrupted cell reports success rather than an error.
- The busy-time helper (`busyHelper`) and the `hera` set-up are Elara's; they do not apply.

It is a way to try Ark's R frontend (its console behaviour, its debugger later) without leaving Jovian's supervisor, not a replacement for Elara.

### Elara and Ark compared

`examples/benchmarks/elara-vs-ark.ts` runs both under Themisto on the same R (run it after `npm run build:lib`; `BENCH_KERNELS=elara` or `ark` for one). On Windows, R 4.6, Ark 0.1.252, one run each (October 2026):

| measure | Elara | Ark |
|---|---|---|
| create -> first result of `1+1` | 267 ms | 428 ms |
| `1+1`, median of 30 | 0.8 ms | 10.0 ms |
| 200 000 lines of output | 0.69 s | 1.96 s |
| 10 MB in one `cat()` | 240 ms | 258 ms |
| a line every 0.1 s, one `cat()` piece each: mean / worst delay until it reaches Node | under 1 ms / 0.1 ms | 51 ms / 95 ms |
| 25 000 `cat()` + `message()` pairs | 0.77 s (stdout and stderr) | 1.45 s (all on stdout) |

Elara sends what R writes after a quiet moment at once and gathers what follows for up to about 50 ms (Ark gathers everything for about 100 ms, the sawtooth of its delays, 6 to 95 ms). So a line R writes in one piece (`cat(sprintf(...))`) arrives within a millisecond, and one written in several (`cat("T", x, "\n")`: R writes each piece) arrives with its last piece, up to about 50 ms later: through a bare client, such lines alternate between about 55 and 11 ms.

The other measures are the kernels' own: a bare client (no Jovian library, `{type: 'execute'}` frames straight to Themisto) gets the same `1+1`, line and flood times. The 10 MB write is the exception: about 240 ms through either, against about 165 ms under Kallichore, so the difference there is the supervisor's.

How the R code is laid out differs too. Both leave the global environment empty and add no package namespace. Ark attaches `tools:positron` (99 objects, nearly all dot-named) and `tools:rstudio`; Elara attaches `tools:jovian` (every name dot-named) and `tools:rstudio`, and replaces `View()` in `utils`. Elara's `tools:jovian` sits right after the global environment because its functions look up R's packages from there; its `tools:rstudio` holds only copies (rstudioapi finds them by the environment's name), so it is attached last, where it can shadow nothing.

### Elara under Positron's supervisor (Kallichore)

Elara starts as a standard Jupyter kernel (`-f <connection_file> --r-home <R>`), so Positron's supervisor, Kallichore (`kcserver`), can run it: start-up, cells, output, `message()` on stderr and plots work. With Kallichore 0.1.68 and the same bare client: start-up about 0.55 s (Ark: 0.82 s), `1+1` 0.5 ms (Ark: 8.6 ms), 200 000 lines 0.63 s (Ark: 1.88 s), the 10 MB write 166 ms (Ark: 172 ms), the 25 000 `cat()` + `message()` pairs 0.76 s (Ark: 1.55 s); lines arrive as under Themisto, a few milliseconds later. Twice, while a script deleted and killed sessions and left others running, `kcserver` panicked in its ZeroMQ library (`zeromq-0.4.1 dealer.rs: not yet implemented`) and the session ended; it was not reproduced with a session left idle, a WebSocket closed, or any of the cells above, so what triggers it is not known.

## Differences at a glance

| | Elara (R) | Carpo (Python) | Callisto (Stata) |
|---|---|---|---|
| Runtime dependency | R (its R code, `hera`, is built in) | CPython 3 with its shared library | Stata 17+, licensed |
| Global scope | `.GlobalEnv` | `__main__` | Stata's dataset in memory, interactive-level macros |
| Rich output | `display_data`, plots (`image/png`), `text/html`, `update_display_data`, `clear_output` (via `hera`) | `execute_result` `text/plain` only — no `display_data` / plots yet | graphs (`image/png`); no `execute_result` |
| stderr stream | messages and warnings | `sys.stderr` | none — everything is stdout |
| Completions | R names | `rlcompleter` (with `(` for callables) | commands, variables, scalars, globals, locals, `r()`/`e()`/`s()` results |
| Inspect | help pages (HTML + text) | signature / docstring (text) | a variable's `describe` + `summarize`, a scalar's or global's value, a command's `which` |
| Comms | yes (`.elara.CommManager`) | no targets can be registered | no |
| Interrupt | user-break flag | SIGINT → `KeyboardInterrupt` | Stata's Break → `r(1)` |
| Answered while a cell runs | interrupt; complete and inspect of package functions (helper R process) | interrupt, complete, inspect, is_complete | interrupt, is_complete |
| Between cells | `later` callbacks (httpuv, promises) run | threads and asyncio tasks run | nothing runs |
| `kernel_info` | `R`, `implementation: "xr"` | `python`, `implementation: "carpo"`, banner `carpo (Python x.y.z)` | `stata`, `implementation: "callisto"`, banner `callisto (Stata 19.5 MP)` |
| Extra environments | `rLibs` | `venvPath`, `pythonPath` | `stataEdition`; ado-paths are Stata's own |

## Known limits (all kernels)

- **One cell at a time.** A kernel runs one execution at a time, and requests other than the ones listed under *Answered while a cell runs* above wait for it. For parallel work, use separate sessions (one process each) or the language's own tools inside a cell: R's `parallel` / `callr` / `future`, Python's threads, asyncio and process pools, Stata/MP's multiple cores.
- **Interrupts cannot break native code** that never returns to the interpreter, nor a read blocked on `input()`.
- **History is in-memory**, holds inputs only, and is lost on restart.
- **No debugger protocol**, no `input_reply` password prompts (`password` is always `false`).
- **Shiny / long calls block the session**; the supervisor's stop path force-kills such a kernel after ~2 s.
- **`inspect_request.detail_level`** is accepted but ignored.
