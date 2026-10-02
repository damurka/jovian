# The calls made for every message, warning, stderr write and output go to Elara directly, not through
# .jv.elara.call(): hera only ever runs inside Elara (it is built into it), and that extra R call showed in a cell that
# calls message() 25 000 times. Display data goes over as R values, which Elara converts to JSON once (jsonFromR() in
# routine.cpp) -- not written as JSON text here to be parsed back there.
# A line in the kernel's log (ELARA_LOG_LEVEL, see Elara's log.hpp): what hera does that nobody sees otherwise --
# a fallback taken, an error caught and set aside. Written only when that level is (Elara checks).
.jv.log.debug <- function(...) {
  invisible(.Call("elara_log", "debug", paste0(...), PACKAGE = "(embedding)"))
}

.jv.log.warning <- function(...) {
  invisible(.Call("elara_log", "warning", paste0(...), PACKAGE = "(embedding)"))
}

.jv.output.stream <- function(name, text) {
  .Call("elara_publish_stream", name, text, PACKAGE = "(embedding)")
}

# JSON text of an R value, written by Elara (no jsonlite): jsonlite's toJSON(auto_unbox = TRUE) rules -- a length-one
# vector is a scalar unless I(), a named list an object, NA null, a raw vector unwrapped base64 (see sexpToJson() in
# Elara's routine.cpp). `null`: what NULL becomes, "list" ({}) or "null".
.jv.json.write <- function(x, null = "list") {
  .jv.elara.call("elara_to_json", .jv.json.prepare(x), null)
}

# An R value from JSON text, read by Elara: jsonlite's fromJSON() rules for objects (named lists) and arrays of
# scalars (vectors); other arrays are lists.
.jv.json.read <- function(text) {
  .jv.elara.call("elara_from_json", text)
}

# What Elara cannot format itself: vectors of a class (Date, POSIXct, difftime, ...) become their text, as jsonlite
# wrote them. Factors, I() and "json" text are left to Elara.
.jv.json.prepare <- function(x) {
  if (is.list(x)) {
    if (length(x)) x[] <- lapply(x, .jv.json.prepare)
    return(x)
  }
  if (is.object(x) && is.atomic(x) && !is.factor(x) && !inherits(x, c("AsIs", "json"))) {
    return(as.character(x))
  }
  x
}

#' Display data
#'
#' @param data data to display
#' @param metadata potential metadata
#'
#' @examples
#' \dontrun{
#'   .elara.display_data(mtcars)
#' }
#'
.elara.display_data <- function(data = NULL, metadata = NULL) {
  invisible(.Call("elara_display_data", .jv.json.prepare(data), .jv.json.prepare(metadata), PACKAGE = "(embedding)"))
}

.elara.update_display_data <- function(data = NULL, metadata = NULL) {
  invisible(.Call("elara_update_display_data", .jv.json.prepare(data), .jv.json.prepare(metadata), PACKAGE = "(embedding)"))
}

#' Clear output
#'
#' @param wait Should this wait
#'
#' @examples
#' \dontrun{
#'   .elara.clear_output()
#' }
#'
#' @return NULL invisibly
.elara.clear_output <- function(wait = FALSE) {
  invisible(.jv.elara.call("elara_clear_output", isTRUE(wait)))
}

#' View
#'
#' A table of the global environment (a data frame or a matrix, named: `View(mtcars)`) opens in the data viewer of
#' the application running the session, when it has one (a "viewData" question, `params = list(name, title)`, that it
#' answers with `{"ok": true}`); anything else, or with no data viewer, is displayed in the output.
#'
#' @param x something to display
#' @param title title of the display
#'
#' @examples
#' \dontrun{
#'   View(mtcars)
#' }
#'
.elara.View <- function(x, title) {
  name <- substitute(x)
  if (is.name(name) && .jv.vars.is_table(x) && exists(as.character(name), envir = globalenv(), inherits = FALSE)) {
    params <- list(name = as.character(name))
    if (!missing(title)) params$title <- as.character(title)
    answer <- .jv.ui.ask("viewData", params, default = NULL)
    if (isTRUE(answer$ok)) return(invisible(x))
  }
  if (!missing(title)) .elara.display_data(list("text/plain" = title))
  .elara.display(x)
  invisible(x)
}
