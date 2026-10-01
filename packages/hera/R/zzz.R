#' @importFrom grDevices pdf png
#' @importFrom utils head tail capture.output
NULL

.jv.display.vignette <- function(x, ...) {
  file <- x$PDF
  if (nzchar(file) == 0) {
    warning(gettextf("vignette %s has no PDF/HTML", sQuote(x$Topic)), call. = FALSE, domain = NA)
    return(invisible(x))
  }

  ext <- tolower(tools::file_ext(file))
  if (ext == "pdf") {
    warning("can't display pdf vignette yet")
    return(invisible(x))
  }

  if (ext == "html") {
    html <- readLines(file.path(x$Dir, "doc", file))

    display_data(
      data = list(
        "text/html" = paste(html, collapse = "\n")
      ),
      metadata = list(
        "text/html" = list(isolated = TRUE)
      )
    )
  }

  invisible(x)
}

NAMESPACE <- environment()
the <- NULL

.onLoad <- function(libname, pkgname) {
    # Elara verification/handshake now happens via is_elara()/.jv.elara.call()
    # below, not here -- this used to be a bare TODO for that, predating
    # those functions.
    NAMESPACE$the <- new.env()
    the$cell_exit <- list()

    ns_utils <- asNamespace("utils")
    get("unlockBinding", envir = baseenv())("print.vignette", ns_utils)

    assign("print.vignette", .jv.display.vignette, ns_utils)
    get("lockBinding", envir = baseenv())("print.vignette", ns_utils)


    .jv.init.options()   
}

.jv.init.options <- function() {
  options(
    device = .jv.graphics.null_device(),
    cli.num_colors = 256L,
    jupyter.plot_mimetypes = c('text/plain', 'image/png'),
    jupyter.plot_scale = 2,

    jupyter.rich_display = TRUE,
    jupyter.base_display_func = display_data,
    jupyter.clear_output_func = clear_output
  )

  repos <- getOption('repos')
  if (identical(repos, c(CRAN = '@CRAN@'))) {
    repos[['CRAN']] <- 'https://cran.r-project.org'
    options(repos = repos)
  }
}

NAMESPACE <- environment()
.jv.call <- function(fn, ...) {
    get(fn, envir = NAMESPACE)(...)
}

#' Is this a running Elara jupyter kernel
#'
#' @return TRUE if the current session is running in an Elara kernel
#'
#' @examples
#' is_elara()
#'
#' @export
is_elara <- function() {
  embedding <- getLoadedDLLs()[["(embedding)"]]
  !is.null(embedding) && "elara_kernel_info_request" %in% names(getDLLRegisteredRoutines(embedding)$.Call)
}

# is_elara() lists every loaded DLL and all of Elara's registered routines,
# which cost more than the routine call itself on every message a cell
# printed; whether R is running inside Elara cannot change, so a yes is kept.
.jv.elara.running <- function() {
  if (isTRUE(the$in_elara)) return(TRUE)
  yes <- is_elara()
  if (yes) the$in_elara <- TRUE
  yes
}

.jv.elara.call <- function(fn, ...) {
  if (.jv.elara.running()) {
    return(.Call(fn, ..., PACKAGE = "(embedding)"))
  }
  stop(sprintf("The \"%s\" routine must be called inside an Elara kernel.", fn), call. = FALSE)
}

# The device R opens when code first draws (options(device)): the session's plot device -- see graphics.R
.jv.graphics.null_device <- function() {
  function(...) .jv.graphics.open_device()
}

#' Display an object
#'
#' IRdisplay's `display()`: IRdisplay (and repr) are loaded when it is first used, not with hera, so a session loads
#' no package it doesn't use.
#'
#' @param ... passed to [IRdisplay::display()]: the object, and optionally `metadata`, `mimetypes`, `error_handler`
#'
#' @examples
#' \dontrun{
#'   display(mtcars)
#' }
#'
#' @export
display <- function(...) {
  if (!requireNamespace("IRdisplay", quietly = TRUE)) {
    stop("display() needs the IRdisplay package: install.packages(\"IRdisplay\")", call. = FALSE)
  }
  IRdisplay::display(...)
}
