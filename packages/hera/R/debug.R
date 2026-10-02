# The debugger's R side (Elara's debugger_r.cpp): breakpoints on R's own browser(), the call stack and variables of a
# stopped session, and evaluating in a frame. Everything here catches its own errors -- some of it runs in the
# browser's context, where an error must not escape.

# A cell's code while debugging: parsed from the file the client knows it by (`path`, see debugger_r.cpp), with a
# browser() before each top-level expression that starts on a breakpoint `lines`; or the parse error's message
.jv.debug.parse <- function(code, path, lines) {
  message <- tryCatch({ parse(text = code); NULL }, error = function(e) paste(conditionMessage(e), collapse = "\n"))
  if (!is.null(message)) return(message)
  exprs <- parse(text = code, keep.source = TRUE, srcfile = srcfilecopy(path, code))
  .jv.debug.instrument(exprs, lines)
}

# The cell's expressions as one braced block, with browser() before those starting on a breakpoint line: R steps
# through a top-level {} (n), so the cell can be stepped from there. (Unchanged when no line has a breakpoint.)
.jv.debug.instrument <- function(exprs, lines) {
  refs <- attr(exprs, "srcref")
  if (!length(lines) || is.null(refs)) return(exprs)
  first <- vapply(refs, function(r) as.integer(r[[1L]]), 0L)
  hit <- first %in% lines
  if (!any(hit)) return(exprs)
  body <- list()
  srcrefs <- list(NULL)
  for (i in seq_along(exprs)) {
    if (hit[[i]]) {
      body <- c(body, list(quote(base::browser())))
      srcrefs <- c(srcrefs, list(refs[[i]]))
    }
    body <- c(body, list(exprs[[i]]))
    srcrefs <- c(srcrefs, list(refs[[i]]))
  }
  block <- as.call(c(as.name("{"), body))
  attr(block, "srcref") <- srcrefs
  attr(block, "srcfile") <- attr(exprs, "srcfile")
  as.expression(list(block))
}

# Breakpoints in functions defined from the cells' files: `spec`, file -> lines, made R's own (utils::setBreakpoint(),
# a trace() calling browser() at that step); those set before are cleared first. Lines outside any function are the
# cell's own (see .jv.debug.instrument()).
.jv.debug.apply_breakpoints <- function(spec) {
  tryCatch({
    for (set in .jv.the$debug_traced) {
      try(utils::setBreakpoint(set$path, set$line, nameonly = FALSE, envir = globalenv(), lastenv = globalenv(),
        verbose = FALSE, clear = TRUE), silent = TRUE)
    }
    .jv.the$debug_traced <- list()
    for (path in names(spec)) {
      for (line in unlist(spec[[path]])) {
        found <- tryCatch(utils::findLineNum(path, line, nameonly = FALSE, envir = globalenv(), lastenv = globalenv()),
          error = function(e) NULL)
        if (!length(found)) next
        utils::setBreakpoint(path, line, nameonly = FALSE, envir = globalenv(), lastenv = globalenv(), verbose = FALSE)
        .jv.the$debug_traced[[length(.jv.the$debug_traced) + 1L]] <- list(path = path, line = line)
      }
    }
  }, error = function(e) .jv.log.warning("setting breakpoints: ", conditionMessage(e)))
  invisible()
}

.jv.debug.location <- function(srcref) {
  if (is.null(srcref) || !inherits(srcref, "srcref")) return(list(path = "", line = 0L, column = 1L))
  file <- attr(srcref, "srcfile")$filename
  list(path = if (is.null(file)) "" else file, line = as.integer(srcref[[1L]]), column = as.integer(srcref[[5L]]))
}

# The calls being debugged, innermost first: name, path, line, column, and each one's environment. Evaluated in the
# browser's context (sys.calls() is the debugged code's, below this function's own call); `path` and `line` are where
# R is stopped (its last "debug at" line).
.jv.debug.stack <- function(path, line) {
  here <- list(path = path, line = as.integer(line), column = 1L)
  tryCatch({
    calls <- sys.calls()
    frames <- sys.frames()
    n <- length(calls) - 1L # this function's own call
    calls <- calls[seq_len(max(0L, n))]
    frames <- frames[seq_len(max(0L, n))]
    # what calls browser() for a breakpoint (trace()'s .doTrace(), eval.parent(), eval()) is not the user's code
    plumbing <- function(cl) {
      head <- cl[[1L]]
      name <- if (is.name(head)) as.character(head) else if (is.call(head) && length(head) == 3L) as.character(head[[3L]]) else ""
      name %in% c(".doTrace", "eval.parent", "eval", "evalq", "browser", ".jv.debug.on_interrupt")
    }
    while (length(calls) && plumbing(calls[[length(calls)]])) {
      calls <- calls[-length(calls)]
      frames <- frames[-length(frames)]
    }
    out <- list()
    envs <- list()
    for (i in rev(seq_along(calls))) {
      # where frame i is: the statement R is at, for the innermost; else where it called the next one
      location <- if (i == length(calls)) here else .jv.debug.location(attr(calls[[i + 1L]], "srcref"))
      out[[length(out) + 1L]] <- c(list(name = paste(deparse(calls[[i]][[1L]], nlines = 1L), collapse = "")), location)
      envs[[length(envs) + 1L]] <- frames[[i]]
    }
    # the cell itself, at the global environment
    # the cell, where its running expression is (Elara keeps that in base::.jv_cell_srcref) -- or, when R is
    # stopped in the cell's own code, where R is
    top <- if (length(calls)) .jv.debug.location(get0(".jv_cell_srcref", envir = baseenv())) else here
    out[[length(out) + 1L]] <- c(list(name = "<cell>"), top)
    envs[[length(envs) + 1L]] <- globalenv()
    list(frames = out, envs = envs)
  }, error = function(e) list(frames = list(), envs = list()))
}

# One value, as the debugger lists it
.jv.debug.describe <- function(x) {
  type <- class(x)[[1L]]
  value <- tryCatch({
    if (is.environment(x)) {
      format(x)
    } else if (is.function(x)) {
      paste(deparse(args(x))[[1L]], collapse = "")
    } else if (is.data.frame(x)) {
      sprintf("data.frame: %d x %d", nrow(x), ncol(x))
    } else if (is.atomic(x) && length(x) <= 10L && is.null(dim(x))) {
      paste(utils::capture.output(cat(format(x), sep = " ")), collapse = " ")
    } else if (is.atomic(x)) {
      sprintf("%s [%s]", type, paste(if (is.null(dim(x))) length(x) else dim(x), collapse = " x "))
    } else if (is.list(x)) {
      sprintf("list [%d]", length(x))
    } else {
      paste(utils::capture.output(print(x)), collapse = " ")
    }
  }, error = function(e) sprintf("<%s>", type))
  if (nchar(value) > 200L) value <- paste0(substr(value, 1L, 197L), "...")
  list(value = value, type = type, expandable = is.environment(x) || (is.list(x) && length(x) > 0L) ||
    (is.atomic(x) && length(x) > 10L))
}

# An environment's or object's variables: rows (name, value, type) and the children that can be opened (or NULL)
.jv.debug.variables <- function(object) {
  tryCatch({
    if (is.environment(object)) {
      names <- ls(object, sorted = TRUE)
      values <- lapply(names, function(n) tryCatch(get(n, envir = object, inherits = FALSE), error = function(e) NULL))
    } else if (is.list(object)) {
      names <- if (is.null(names(object))) paste0("[[", seq_along(object), "]]") else ifelse(nzchar(names(object)), names(object), paste0("[[", seq_along(object), "]]"))
      values <- as.list(object)
    } else if (is.atomic(object)) {
      shown <- seq_len(min(length(object), 100L))
      names <- paste0("[", shown, "]")
      values <- as.list(object[shown])
    } else {
      names <- character()
      values <- list()
    }
    rows <- lapply(seq_along(names), function(i) c(list(name = names[[i]]), .jv.debug.describe(values[[i]])[c("value", "type")]))
    children <- lapply(values, function(v) if (.jv.debug.describe(v)$expandable) v else NULL)
    list(rows = rows, children = children)
  }, error = function(e) list(rows = list(), children = list()))
}

# An expression evaluated in a frame's environment: its printed value, and the value itself when it can be opened
.jv.debug.evaluate <- function(expression, env) {
  tryCatch({
    value <- eval(parse(text = expression), envir = env)
    list(result = paste(utils::capture.output(print(value)), collapse = "\n"),
      child = if (.jv.debug.describe(value)$expandable) value else NULL)
  }, error = function(e) list(result = paste("Error:", conditionMessage(e)), child = NULL))
}

# R's global interrupt handler: a pause the debugger asked for stops here, in browser(), and goes on from there
.jv.debug.on_interrupt <- function(cnd) {
  if (!isTRUE(.Call("elara_debug_take_pause", PACKAGE = "(embedding)"))) return()
  browser()
  invokeRestart("resume")
}
