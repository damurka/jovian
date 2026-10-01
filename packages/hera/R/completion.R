# pkg:::fun, for the internal functions of utils that complete code
.jv.complete.internal <- function(pkg, fun) {
  get(fun, envir = asNamespace(pkg))
}

.jv.complete.assign_line_buffer    <- .jv.complete.internal("utils", ".assignLinebuffer")
.jv.complete.assign_end           <- .jv.complete.internal("utils", ".assignEnd")
.jv.complete.guess_token  <- .jv.complete.internal("utils", ".guessTokenFromLine")
.jv.complete.complete_token       <- .jv.complete.internal("utils", ".completeToken")
.jv.complete.retrieve <- .jv.complete.internal("utils", ".retrieveCompletions")

#' Code completion
#'
#' @param code R code to complete
#' @param cursor_pos position of the cursor
#'
#' @examples
#' complete("rnorm(")
#'
#' @return a list that contains potential completions as the first item
#'
#' @export
complete <- function(code, cursor_pos = nchar(code)) {
    # Find which line we're on and position within that line
    lines <- strsplit(code, '\n', fixed = TRUE)[[1]]
    chars_before_line <- 0L
    for (line in lines) {
        new_cursor_pos <- cursor_pos - nchar(line) - 1L # -1 for the newline
        if (new_cursor_pos < 0L) {
            break
        }
        cursor_pos <- new_cursor_pos
        chars_before_line <- chars_before_line + nchar(line) + 1L
    }

    # guard from errors when completion is invoked in empty cells
    if (is.null(line)) {
        line <- ''
    }

    .jv.complete.assign_line_buffer(line)
    .jv.complete.assign_end(cursor_pos)

    info <- .jv.complete.guess_token(update = FALSE)
    .jv.complete.guess_token()
    .jv.complete.complete_token()

    start_position <- chars_before_line + info$start
    comps <- .jv.complete.retrieve()

    list(
      comps,
      c(start_position, start_position + nchar(info$token))
    )
}
