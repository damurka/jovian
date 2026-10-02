#' MIME types supported by an object
#'
#' @param x an object
#'
#' @examples
#' .elara.mime_types(letters)
#' .elara.mime_types(mtcars)
#'
#' @return a character vector of its supported mime types
.elara.mime_types <- function(x) {
  UseMethod(".elara.mime_types")
}

.elara.mime_types.default <- function(x) {
  "text/plain"
}

.elara.mime_types.htmlwidget <- function(x) {
  c("text/plain", "text/html")
}

.elara.mime_types.shiny.tag.list <- function(x) {
  c("text/plain", "text/html")
}

.elara.mime_types.shiny.tag <- function(x) {
  c("text/plain", "text/html")
}

# R help objects. Without this, mime_types.default returns "text/plain", which
# repr::repr_text renders via tools::Rd2txt. Rd2txt emits nroff overstrike
# ("_\bX" underline, "X\bX" bold), and Jupyter's stdout stream does not
# interpret backspaces, so ?lm displays raw "_ l_ m" garbage. Advertising
# text/html lets the frontend pick the HTML rendering instead.
.elara.mime_types.help_files_with_topic <- function(x) {
  c("text/plain", "text/html")
}

#' bundle an object
#'
#' @param x an object
#' @param mimetypes mime types
#' @param ... extra currently unused parameters
#'
#' @examples
#' .elara.mime_bundle(letters)
#'
#' @seealso IRdisplay::prepare_mimebundle, which does it for objects base R can't
#'
.elara.mime_bundle <- function(x, mimetypes = .elara.mime_types(x), ...) {
  UseMethod(".elara.mime_bundle")
}

# Whether the bundle needs repr (through IRdisplay): a mime type other than plain text. Plain text is what print()
# writes, as R's console (and Ark) show a value -- a data frame or matrix included, which repr laid out its own way.
# Help pages are rendered with tools' Rd2txt and Rd2HTML, as repr does.
.jv.display.needs_repr <- function(x, mimetypes) {
  if (inherits(x, "help_files_with_topic")) return(!all(mimetypes %in% c("text/plain", "text/html")))
  !all(mimetypes == "text/plain")
}

.elara.mime_bundle.default <- function(x, mimetypes = .elara.mime_types(x), ...) {
  if (.jv.display.needs_repr(x, mimetypes)) {
    if (requireNamespace("IRdisplay", quietly = TRUE)) {
      return(IRdisplay::prepare_mimebundle(x, mimetypes = mimetypes, ...))
    }
    # without IRdisplay (it is optional), what base R can show: the text print() writes
    if (!isTRUE(.jv.the$warned_no_irdisplay)) {
      .jv.the$warned_no_irdisplay <- TRUE
      .jv.log.warning("IRdisplay is not installed: data frames, widgets and the like are shown as text")
    }
    mimetypes <- "text/plain"
  }
  data <- .jv.utils.named_list()
  for (mime in mimetypes) {
    data[[mime]] <- if (inherits(x, "help_files_with_topic")) {
      .jv.display.help_text(x, html = identical(mime, "text/html"))
    } else {
      paste(utils::capture.output(print(x)), collapse = "\n")
    }
  }
  list(data = data, metadata = NULL)
}

# A help page as text or as the body of its HTML page: what repr's repr_text/repr_html give for one.
.jv.display.help_text <- function(x, html) {
  topic <- attr(x, "topic")
  paths <- as.character(x)
  if (length(paths) == 0) {
    return(paste(gettextf("No documentation for %s in specified packages and libraries:", sQuote(topic)),
      gettextf("you could try %s", sQuote(paste0("??", topic))), sep = "\n"))
  }
  file <- paths[[1]]
  rd <- get(".getHelpFile", envir = asNamespace("utils"))(file)
  package <- basename(dirname(dirname(file)))
  # as text, without Rd2txt's underlined titles: overstrikes ("_\bA") that show as "_A_r_i_t_h" in a notebook
  output <- utils::capture.output(if (html) {
    tools::Rd2HTML(rd, package = package, outputEncoding = "UTF-8")
  } else {
    tools::Rd2txt(rd, package = package, outputEncoding = "UTF-8", options = list(underline_titles = FALSE))
  })
  if (html) {
    head_end <- which(startsWith(output, "</head><body>"))
    body_end <- which(endsWith(output, "</body></html>"))
    output <- output[-c(seq_len(head_end), body_end)]
  }
  paste(output, collapse = "\n")
}
