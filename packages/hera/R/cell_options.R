#' Options for current jupyter cell
#'
#' @param ... options to set locally to the notebook cell, as for [options()]: set back when the cell is done.
#'
#' @examples
#' \dontrun{
#'   cell_options(repr.plot.bg = "gray")
#' }
#'
#' @export
cell_options <- function(...) {
    old <- options(...)
    # set back when the cell is done (.jv.repl.cell_done())
    the$cell_exit[[length(the$cell_exit) + 1L]] <- function() options(old)
    invisible(old)
}
