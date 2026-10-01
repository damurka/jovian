# hera (development version)

* The session's variables, for a frontend's variables pane and data viewer (`.jv.rpc.var_list`, `.jv.rpc.var_table`; Jovian's `Session.listVariables()`, `readTable()`): the objects of the global environment with their class, size and a one-line preview, and pages of a data frame's or a matrix's rows as text, as R prints them. Active bindings are listed without being run.

* Plots are recorded on ragg's `agg_record()` when ragg (>= 1.4.0) is installed, as Ark does, and drawn with `agg_png()`/`agg_jpeg()`: a plot is drawn once, into the image sent, not first on a device that draws pixels nobody sees. A 100 000-point plot takes 122 ms instead of 585 ms (Ark: 101 ms). Without ragg, `png()` is used as before. The device opens when code first draws (`options(device)`), not at every cell, so a session that never plots loads nothing for it. `options(jupyter.plot_ragg = FALSE)` draws with `png()` instead: ragg leaves out the text of fonts with bitmap letters (Calibri, Cambria) at 8 to 10 points.
* `host_notify()` and `host_ask()`: requests from R code to the application running the session (a Shiny app asking its host to print, say), through Jovian's Session `'ui'` event; `host_ask()` waits for the answer, also while a cell runs.
* The same plot drawn again in a new cell (`plot(1:10)` run twice) is sent again: a new page is a new plot even when it draws what the last one did.

* A debugger, on R's own browser(), through the Jupyter debug protocol (JEP 47; `docs/guides/debugging.md`): breakpoints in functions (R's `setBreakpoint()`) and on a cell's own lines, the call stack with files and lines, variables, evaluating in a frame, stepping (next, step in, step out), continue and pause. Debug requests are answered while a cell runs or is stopped (on the control channel).
* The RStudio API for rstudioapi (`tools:rstudio`, `.rs.api.*`): requests to the host's UI -- open a file, show a URL, prompts, questions, the editor's context -- as Jovian's Session 'ui' events; `rstudioapi::isAvailable()` is TRUE.
* Package management for a frontend (`.jv.rpc.pkg_list`, `is_installed`, `pkg_outdated`, `pkg_search`, `install_packages`, `remove_packages`) and R's help server (`.jv.rpc.help_server`, `help_url`), served while the session is idle.

* Cells are run by R's own console loop, as in Ark: Elara evaluates each expression and hands R's console its value, so R itself prints it, sets `.Last.value`, reports warnings and runs task callbacks; errors are caught by a global calling handler (`.jv.errors.handler`). Behaviour is R's: `geterrmessage()`, `try()` output on stderr, top-level `on.exit()` ignored, rlang backtraces from the call that raised the error. HTML widgets, Shiny tags and help pages are shown through print methods put in place for them (`.jv.display.*`).
* Comms are held by Elara in C++: a comm is its id (class `Comm`), a received message a list (class `Message`); handlers are kept from R's garbage collector (they could be collected before), and an error in one is logged.
* Hidden functions follow one naming convention, `.jv.<area>.<name>` (`.jv.repl.*`, `.jv.errors.*`, `.jv.graphics.*`, `.jv.display.*`, `.jv.comm.*`), as Ark's `.ps.*`.

* A session behaves as R's console and Ark do. Warnings are R's own: `options(warn)` applies (`-1` hides them, `1` prints them at once, `2` makes them errors), they are reported when a top-level expression ends in R's words (`In f() : w`, `There were 50 or more warnings ...`), and `warnings()` shows them. After an error `traceback()` shows its calls and `options(error)` has run. The plot device lasts the session, so a later cell can add to a plot and `par()` settings last; a big plot costs later cells nothing (Elara checks the display list in constant time). A value is shown as `print()` shows it -- a data frame in R's layout, not repr's -- and a lattice plot is drawn (only ggplot was). A top-level `on.exit()` does nothing.

* The kernel logs one line per event at a level (`ELARA_LOG_LEVEL`: debug, info, warning, error) instead of unlabelled start-up dumps, and hera can log through it (`elara_log`). On Windows the lines written once R has started (hera loaded, registered with the supervisor, errors) reach Jovian again: they used to go to the hidden console R's start-up opens. An error in a comm handler is caught and logged instead of unwinding through the kernel's C++ code. Data frames and widgets are shown as text, with one logged warning, when IRdisplay isn't installed, instead of failing the cell.
* Removed code nothing used: `kernel_info_request()`, `is_complete_request()`, the `log_*()` functions of `log.R`, and `session_state()` (Elara answers that query itself).

* hera is built into Elara instead of installed: `cmake/EmbedHera.cmake` turns its R files into part of the kernel, which loads them at start-up as the namespace `hera` (exports attached as `tools:hera`). Nothing is installed into R any more -- Jovian's one-time setup that ran Rscript to install hera and its CRAN dependencies is gone -- so a first R session starts at once, offline too. `heraSrcPath` now only points Elara at a `packages/hera` folder to read the files from, for development.

* Output reaches Elara with less work: display data, a cell's value and inspect replies go over as R values that Elara converts to JSON once (not written as JSON text in R and parsed back), and the calls made for every message and output go to Elara directly. 2 000 `display_data()` calls take 38% less time, an 8 MB HTML output 25% less, 25 000 `message()` calls 8% less (`examples/benchmarks/hera-overhead.ts`). The session state Jovian asks for after each cell is read by Elara itself.
* Cells run with base R alone: hera no longer uses evaluate or repr, and plots are drawn with R's own devices. A session loads fewer packages before the user's code does, the first step towards loading none (as Ark does), so that any package can be installed or updated from a session, even on Windows.
* Tracebacks show only the cell's own calls, with where they are in the cell (`f() at [3]#2`), not the internals of evaluate.
* An rlang error's message is now its `evalue` (it was empty).
* A plot drawn before an error in the same cell is shown, as R's console would.
* IRdisplay and repr are loaded only when something needs them -- a data frame, an HTML widget, `display()`, `View()` -- not with hera. Other values are shown with base R, exactly as repr showed them.
* JSON (display data, comm messages, inspect replies, the session state Jovian reads after each cell) is written and read by Elara itself, not jsonlite, following jsonlite's rules. Raw vectors are plain base64; a comm message's mixed array (`[1, "a"]`) is a character vector, as jsonlite gave. NaN and Inf are `null` (jsonlite wrote the strings "NaN" and "Inf", which aren't numbers either). Needs an Elara built with `elara_to_json`/`elara_from_json`.
* hera imports no CRAN package any more: comms (`CommManager`, `Comm`, messages) are base R environments with the same fields and methods instead of R6 classes, and `cell_options()`, completion and the Elara check use base R instead of rlang, cli and glue. A new session has only R's base packages and hera loaded, as Ark has only R's base packages. IRdisplay stays in Imports, loaded only to display data frames, widgets and the like.
* Inspecting a name (Shift-Tab, hover) loads nothing new: a help page is rendered with tools' Rd2HTML and Rd2txt, as repr did, and values as text, unless repr is loaded already. Help pages as text no longer show underline overstrikes (`_A_r_i_t_h`).

* Printing a lot of output is up to about four times faster: it is no longer also written to a temporary file and read back after every expression (about 150 ms per MB), only to be discarded.
* Every dependency now has a minimum version.
* Calls into Elara (every message, warning and stderr write) no longer re-check that R runs inside Elara each time; a cell calling `message()` 25 000 times ran several times slower because of it.

* Output from a single long-running expression (a loop of print() calls) now streams as it is produced instead of arriving when the expression finishes.

# hera 0.1.1

* Initial CRAN submission.
