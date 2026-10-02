# A cell is run by R's own console loop, as in R's console and Ark: Elara (RInterpreter::readConsole()) evaluates
# the cell's expressions one at a time and hands R's console each value, so R prints it, sets .Last.value, reports
# the warnings and runs task callbacks itself -- options(warn), warnings(), addTaskCallback() all as in R. What it
# prints goes straight to Elara's console hook, published as it is written. The last expression's printed value is
# the cell's result. The R here is what Elara calls around that: parsing the cell, the error handler, and the start
# and end of a cell (the plot, cell_options()).

# The cell's code as expressions, keeping their source (a function defined in it says where it is: "at [3]#2" in a
# traceback), or the parse error's message (the cell's error: incomplete code is one too).
.jv.repl.parse <- function(code, execution_count) {
  # the message as before ("<text>:1:4: unexpected '*'"), from a parse without the cell's name
  message <- tryCatch({ parse(text = code); NULL }, error = function(e) paste(conditionMessage(e), collapse = "\n"))
  if (!is.null(message)) return(message)
  parse(text = code, keep.source = TRUE, srcfile = srcfilecopy(sprintf("[%d]", execution_count), code))
}

.jv.repl.cell_start <- function(silent) {
  .jv.the$cell_plot <- NULL
  .jv.the$cell_exit <- list()
  .jv.the$cell_silent <- silent
  invisible()
}

.jv.repl.cell_done <- function(silent, failed) {
  # an expression that failed left its warnings untold (R reports them when an expression completes)
  if (failed && !silent) .jv.repl.flush_warnings()
  if (!silent) tryCatch(.jv.graphics.cell_done(), error = function(e) .jv.log.warning("sending the plot: ", conditionMessage(e)))
  # cell_options() undone
  for (restore in rev(.jv.the$cell_exit)) tryCatch(restore(), error = function(e) NULL)
  .jv.the$cell_exit <- list()
  invisible()
}

# R's own report of warnings it kept ("Warning message:\nIn f() : w", "There were 50 or more warnings ..."), which also
# keeps them for warnings(). That printer is meant for after an error message, so begins "In addition: ", dropped
# here (up to the first colon: it is translated).
.jv.repl.flush_warnings <- function() {
  text <- utils::capture.output(.Internal(printDeferredWarnings()), type = "message")
  if (length(text)) {
    text[[1L]] <- sub("^[^:]*:\\s*", "", text[[1L]])
    .jv.output.stream("stderr", paste0(paste(text, collapse = "\n"), "\n"))
  }
}

# The ANSI colour of cli::col_red(), which error tracebacks were printed with.
.jv.errors.red <- function(text) paste0("\033[31m", text, "\033[39m")

# The report of a cell's error: its message, and its calls from the cell's own expression down to where it was
# signalled, outermost first, as "2: g()" / "1: stop(\"e\") at [4]#1".
.jv.errors.report <- function(e, calls) {
  if (inherits(e, "rlang_error") && isNamespaceLoaded("rlang")) {
    # rlang's own report, with its backtrace; and kept for rlang::last_error()
    try(assign("last_error", e, envir = get("the", envir = asNamespace("rlang"))), silent = TRUE)
    report <- tryCatch(format(e, backtrace = TRUE), error = function(err) conditionMessage(e))
    return(list(evalue = paste(conditionMessage(e), collapse = "\n"), traceback = paste(report, collapse = "\n")))
  }
  # an error in the cell's own top-level call (log(-"a")) has no frame of its own: its call stands in
  call <- conditionCall(e)
  if (!length(calls) && !is.null(call)) calls <- list(call)
  stack <- if (length(calls)) utils::capture.output(traceback(calls, max.lines = 1L)) else character()
  evalue <- paste(conditionMessage(e), collapse = "\n")
  list(evalue = evalue, traceback = c(
    .jv.errors.red("--- Error"),
    evalue,
    "",
    .jv.errors.red("--- Traceback (most recent call last)"),
    stack
  ))
}

# R's global error handler (globalCallingHandlers(), installed at start-up by .jv.errors.install()), as Ark's: an
# error in a cell's code -- one its own handlers (tryCatch()) left alone -- is reported with the cell's reply, not
# printed by R. traceback() is set, options(error) runs, and the evaluation is abandoned (the "abort" restart).
# Any other error (hera's own, while answering a request) is left to R.
.jv.errors.handler <- function(cnd) {
  if (!.Call("elara_cell_error_wanted", PACKAGE = "(embedding)")) return()
  calls <- sys.calls()
  # this handler's frame, and .handleSimpleError() for an error raised in C
  calls <- calls[-length(calls)]
  if (length(calls) && identical(calls[[length(calls)]][[1L]], quote(.handleSimpleError))) calls <- calls[-length(calls)]
  report <- .jv.errors.report(cnd, calls)
  .Call("elara_record_cell_error", report$evalue, as.character(report$traceback), PACKAGE = "(embedding)")

  # for traceback() in a later cell: the calls, innermost first, as R keeps them (the binding exists from Elara's
  # start-up: base R allows no new one)
  call <- conditionCall(cnd)
  traced <- if (length(calls)) calls else if (!is.null(call)) list(call) else list()
  tryCatch(assign(".Traceback", rev(traced), envir = baseenv()),
    error = function(err) .jv.log.debug("traceback() not set: ", conditionMessage(err)))
  handler <- getOption("error")
  if (!is.null(handler)) {
    tryCatch(if (is.function(handler)) handler() else eval(handler, globalenv()), error = function(e) NULL)
  }
  invokeRestart("abort")
}

.jv.errors.install <- function() {
  # (with the handlers the session has already, as Ark does: globalCallingHandlers(NULL) returns and removes them)
  existing <- globalCallingHandlers(NULL)
  do.call(globalCallingHandlers, c(existing, list(error = .jv.errors.handler, interrupt = .jv.debug.on_interrupt)))
  invisible()
}

# Values only a frontend can show -- an HTML widget, Shiny tags, a help page -- are shown when printed (by R's
# console, or print()) as display data, as Ark does: their own print methods would open a browser or a pager. Put in
# place for their classes now and whenever the package defining their print method is loaded.
.jv.display.print <- function(x, ...) {
  bundle <- .elara.mime_bundle(x)
  .elara.display_data(bundle$data, bundle$metadata)
  invisible(x)
}

.jv.display.OVERRIDES <- list(
  utils = "help_files_with_topic",
  htmltools = c("shiny.tag", "shiny.tag.list"),
  htmlwidgets = "htmlwidget"
)

.jv.display.install <- function() {
  for (package in names(.jv.display.OVERRIDES)) {
    classes <- .jv.display.OVERRIDES[[package]]
    override <- function(...) {
      for (class in classes) registerS3method("print", class, .jv.display.print, envir = asNamespace("base"))
    }
    environment(override) <- list2env(list(classes = classes), parent = environment(.jv.display.install))
    if (isNamespaceLoaded(package)) override()
    setHook(packageEvent(package, "onLoad"), override, "append")
  }
  invisible()
}
