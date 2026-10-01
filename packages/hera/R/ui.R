# The host's UI, for R code: what the rstudioapi package asks of RStudio -- open a file, show a URL, ask a question,
# read the editor -- asked of the application running the session (through Jovian's Session 'ui' event), as Ark does
# for Positron. A request either only tells the host (.jv.ui.notify(): sent over the "jovian.ui" comm the kernel
# opens) or waits for its answer (.jv.ui.ask(): an input_request with `jovian_ui` in its content, answered like a
# readline() prompt -- the one way a running cell can wait for the frontend). A host that does not handle them answers
# nothing: questions then get their default.
#
# The RStudio API itself is the .rs.api.* functions in "tools:rstudio", where rstudioapi looks for them;
# rstudioapi::isAvailable() is made to say TRUE once rstudioapi is loaded (as Ark does: .Platform$GUI stays as it
# is, so code that checks for RStudio itself is not misled).

.jv.ui.TARGET <- "jovian.ui"

# Tells the host's UI: `method` with `params` (a named list)
.jv.ui.notify <- function(method, params = .jv.utils.named_list()) {
  comm <- the$ui_comm
  if (is.null(comm) || is.null(comm$target_name)) {
    comm <- CommManager$new_comm(.jv.ui.TARGET)
    if (is.null(comm)) return(invisible(FALSE))
    comm$open(list())
    the$ui_comm <- comm
  }
  comm$send(list(method = method, params = params))
  invisible(TRUE)
}

# Asks the host's UI and waits: its answer (JSON, read), or `default` when it gave none
.jv.ui.ask <- function(method, params = .jv.utils.named_list(), default = NULL, password = FALSE) {
  reply <- .Call("elara_ui_ask", method, params, password, PACKAGE = "(embedding)")
  if (is.null(reply) || !nzchar(reply)) return(default)
  value <- tryCatch(.jv.json.read(reply), error = function(e) reply)
  if (is.null(value)) default else value
}

#' Requests to the application running the session
#'
#' What R code asks of the application (the host) that runs its session, through Jovian's Session `'ui'` event:
#' `host_notify()` only tells it (`method` with `params`) and returns at once; `host_ask()` waits for its answer, as
#' `readline()` waits for the user's -- it works while a cell runs, a Shiny app's included. The methods are the
#' host's: a host that does not know one ignores a notification and answers a question with nothing.
#'
#' @param method The request's name, as the host knows it (`"myapp.print"`, say).
#' @param params Its parameters: a named list, sent as a JSON object.
#' @param default What `host_ask()` returns when the host gives no answer.
#' @return `host_notify()`: whether the request was sent (`FALSE` outside a Jovian kernel), invisibly.
#'   `host_ask()`: the host's answer (JSON, read as by `jsonlite::fromJSON(simplifyVector = FALSE)`), or `default`.
#' @examples
#' \dontrun{
#' host_notify("myapp.openChat", list(prompt = "Explain this chart"))
#' pdf <- host_ask("myapp.print", list(html = "report.html"), default = NULL)
#' }
#' @export
host_notify <- function(method, params = list()) {
  if (!is_elara()) return(invisible(FALSE))
  .jv.ui.notify(method, .jv.ui.params(params))
}

#' @rdname host_notify
#' @export
host_ask <- function(method, params = list(), default = NULL) {
  if (!is_elara()) return(default)
  .jv.ui.ask(method, .jv.ui.params(params), default = default)
}

# Parameters as a JSON object, even when empty
.jv.ui.params <- function(params) {
  if (!length(params)) return(.jv.utils.named_list())
  if (is.null(names(params)) || any(!nzchar(names(params)))) stop("`params` must be a named list", call. = FALSE)
  as.list(params)
}

.jv.ui.install <- function() {
  # a target for the comm the kernel opens to the host (a comm_open from a frontend to it is answered too)
  CommManager$register_comm_target(.jv.ui.TARGET, function(comm, message) {})

  shim <- new.env(parent = emptyenv())
  for (name in grep("^\\.rs\\.api\\.", ls(asNamespace("hera"), all.names = TRUE), value = TRUE)) {
    assign(name, get(name, envir = asNamespace("hera")), envir = shim)
  }
  attached <- attach(shim, name = "tools:rstudio", pos = length(search()) - 1L, warn.conflicts = FALSE)
  lockEnvironment(attached, bindings = TRUE)

  available <- function(...) {
    ns <- asNamespace("rstudioapi")
    if (!exists("isAvailable", envir = ns, inherits = FALSE)) return()
    unlockBinding("isAvailable", ns)
    on.exit(lockBinding("isAvailable", ns))
    body(ns$isAvailable) <- TRUE
  }
  if (isNamespaceLoaded("rstudioapi")) available()
  setHook(packageEvent("rstudioapi", "onLoad"), available, "append")
  invisible()
}

# ---- positions and ranges: rstudioapi's (rows and columns from 1) <-> the host's ({line, character} from 0) ----

.jv.ui.position <- function(p) list(line = as.integer(p[[1L]]) - 1L, character = as.integer(p[[2L]]) - 1L)

# A location rstudioapi gives (a position, a range, numbers, or a list of them) as host ranges
.jv.ui.ranges <- function(location) {
  one <- function(x) {
    if (inherits(x, "document_range")) return(list(start = .jv.ui.position(x$start), end = .jv.ui.position(x$end)))
    x <- unlist(x)
    if (length(x) == 2L) { p <- .jv.ui.position(x); return(list(start = p, end = p)) }
    if (length(x) == 4L) return(list(start = .jv.ui.position(x[1:2]), end = .jv.ui.position(x[3:4])))
    stop("not a document position or range", call. = FALSE)
  }
  if (inherits(location, c("document_position", "document_range")) || (is.numeric(location) && length(location) %in% c(2L, 4L))) {
    return(list(one(location)))
  }
  lapply(location, one)
}

# The host's editor context ({id, path, contents: lines, selections: [{start, end, text}]}) as rstudioapi's
.jv.ui.document_context <- function(context) {
  if (is.null(context) || !length(context)) return(NULL)
  api <- asNamespace("rstudioapi")
  position <- function(p) api$document_position(row = p$line + 1, column = p$character + 1)
  selections <- lapply(context$selections, function(s) {
    list(range = api$document_range(start = position(s$start), end = position(s$end)), text = s$text %||% "")
  })
  structure(list(
    id = context$id %||% "",
    path = context$path %||% "",
    contents = as.character(unlist(context$contents)),
    selection = structure(selections, class = "document_selection")
  ), class = "document_context")
}

`%||%` <- function(a, b) if (is.null(a)) b else a

# ---- the RStudio API (rstudioapi::callFun()) ---------------------------------------------------------------------

.rs.api.getVersion <- function() package_version(Sys.getenv("JOVIAN_HOST_VERSION", "2025.1.0"))

.rs.api.getMode <- function() Sys.getenv("JOVIAN_HOST_MODE", "desktop")

.rs.api.versionInfo <- function() {
  list(citation = NULL, mode = .rs.api.getMode(), version = .rs.api.getVersion(),
    long_version = Sys.getenv("JOVIAN_HOST_VERSION", "2025.1.0"))
}

.rs.api.viewer <- function(url, height = NULL) {
  .jv.ui.notify("viewer", list(url = url, height = height))
}

.rs.api.navigateToFile <- function(file = character(0), line = -1L, column = -1L, moveCursor = TRUE) {
  .jv.ui.notify("navigateToFile", list(file = normalizePath(file, winslash = "/", mustWork = FALSE),
    line = as.integer(line), column = as.integer(column), moveCursor = isTRUE(moveCursor)))
}

.rs.api.documentNew <- function(text, type = c("r", "rmarkdown", "sql"), position = NULL, execute = FALSE) {
  type <- match.arg(type)
  .jv.ui.notify("documentNew", list(text = paste(text, collapse = "\n"), type = type, execute = isTRUE(execute)))
}

.rs.api.getActiveDocumentContext <- function() {
  .jv.ui.document_context(.jv.ui.ask("getActiveDocumentContext"))
}

.rs.api.getSourceEditorContext <- function(id = NULL) {
  .jv.ui.document_context(.jv.ui.ask("getSourceEditorContext", list(id = id)))
}

.rs.api.documentPath <- function(id = NULL) {
  context <- .jv.ui.ask("getSourceEditorContext", list(id = id))
  context$path
}

.rs.api.insertText <- function(location = NULL, text = NULL, id = NULL) {
  # rstudioapi::insertText("foo"): at the selections
  if (is.null(text)) {
    text <- location
    location <- NULL
  }
  ranges <- if (is.null(location)) NULL else .jv.ui.ranges(location)
  .jv.ui.notify("insertText", list(ranges = ranges, text = as.list(as.character(text)), id = id))
  invisible(list(ranges = ranges, text = text, id = id))
}

.rs.api.modifyRange <- function(location = NULL, text = NULL, id = NULL) .rs.api.insertText(location, text, id)

.rs.api.setSelectionRanges <- function(ranges, id = NULL) {
  .jv.ui.notify("setSelectionRanges", list(ranges = .jv.ui.ranges(ranges), id = id))
}

.rs.api.documentSave <- function(id = NULL) isTRUE(.jv.ui.ask("documentSave", list(id = id), default = FALSE))

.rs.api.documentSaveAll <- function() isTRUE(.jv.ui.ask("documentSaveAll", default = FALSE))

.rs.api.executeCommand <- function(commandId, quiet = FALSE) {
  .jv.ui.notify("executeCommand", list(command = commandId, quiet = isTRUE(quiet)))
}

.rs.api.sendToConsole <- function(code, execute = TRUE, echo = TRUE, focus = TRUE) {
  .jv.ui.notify("sendToConsole", list(code = paste(code, collapse = "\n"), execute = isTRUE(execute),
    echo = isTRUE(echo), focus = isTRUE(focus)))
}

.rs.api.restartSession <- function(command = "") {
  .jv.ui.notify("restartSession", list(command = command))
}

.rs.api.openProject <- function(path = NULL, newSession = FALSE) {
  .jv.ui.notify("openProject", list(path = path, newSession = isTRUE(newSession)))
}

.rs.api.getActiveProject <- function() .jv.ui.ask("getActiveProject")

.rs.api.previewRd <- function(rdFile) {
  .jv.ui.notify("previewRd", list(path = normalizePath(rdFile, winslash = "/", mustWork = FALSE)))
}

.rs.api.readRStudioPreference <- function(name, default) .jv.ui.ask("readPreference", list(name = name), default = default)

.rs.api.showDialog <- function(title, message, url = "") {
  invisible(.jv.ui.ask("showDialog", list(title = title, message = message, url = url)))
}

.rs.api.showQuestion <- function(title, message, ok = NULL, cancel = NULL) {
  isTRUE(.jv.ui.ask("showQuestion", list(title = title, message = message, ok = ok, cancel = cancel), default = FALSE))
}

.rs.api.showPrompt <- function(title, message, default = NULL) {
  .jv.ui.ask("showPrompt", list(title = title, message = message, default = default), default = NULL)
}

.rs.api.askForPassword <- function(prompt = "Please enter your password") {
  .jv.ui.ask("askForPassword", list(prompt = prompt), default = NULL, password = TRUE)
}
