publish_stream <- function(name, text) {
  hera_dot_call("datasuite_publish_stream", name, text)
}

# jsonlite::toJSON() base64-encodes raw vectors (e.g. plot image bytes) via
# jsonlite::base64_enc(), which always MIME-wraps its output with embedded
# newlines and offers no argument to disable it. Those newlines survive
# JSON string-escaping as literal "\n" sequences and, once unescaped by a
# downstream JSON parser, produce a base64 payload containing real newline
# characters -- which a strict (non-whitespace-tolerant) base64 decoder on
# the receiving end will reject. Recursively re-encode any raw elements
# with jsonlite's own encoder and strip the wrapping newlines before they
# ever reach toJSON(), so raw data becomes a plain, unwrapped base64 string.
sanitize_raw_for_json <- function(x) {
  if (is.raw(x)) {
    gsub("\n", "", jsonlite::base64_enc(x), fixed = TRUE)
  } else if (is.list(x)) {
    lapply(x, sanitize_raw_for_json)
  } else {
    x
  }
}

#' Display data
#'
#' @param data data to display
#' @param metadata potential metadata
#'
#' @examples
#' \dontrun{
#'   display_data(mtcars)
#' }
#'
#' @export
display_data <- function(data = NULL, metadata = NULL) {
  invisible(hera_dot_call("datasuite_display_data",
                          enc2utf8(toJSON(sanitize_raw_for_json(data), auto_unbox = TRUE)),
                          enc2utf8(toJSON(metadata, auto_unbox = TRUE))))
}

update_display_data <- function(data = NULL, metadata = NULL) {
  invisible(hera_dot_call("datasuite_update_display_data",
                          enc2utf8(toJSON(sanitize_raw_for_json(data), auto_unbox = TRUE)),
                          enc2utf8(toJSON(metadata, auto_unbox = TRUE))))
}

kernel_info_request <- function() {
  hera_dot_call("datasuite_kernel_info_request")
}


#' Clear output
#'
#' @param wait Should this wait
#'
#' @examples
#' \dontrun{
#'   clear_output()
#' }
#'
#' @return NULL invisibly
#' @export
clear_output <- function(wait = FALSE) {
  invisible(hera_dot_call("datasuite_clear_output", isTRUE(wait)))
}

is_complete_request <- function(code) {
  hera_dot_call("datasuite_is_complete_request", code)
}

#' View
#'
#' @param x something to display
#' @param title title of the display
#'
#' @examples
#' \dontrun{
#'   View(mtcars)
#' }
#'
#' @export
View <- function(x, title) {
  if (!missing(title)) IRdisplay::display_text(title)
  IRdisplay::display(x)
  invisible(x)
}