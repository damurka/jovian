# Comms -- kernel <-> frontend channels -- for R code: the Jupyter comm API of IRkernel's and xeus-r's kernels
# (CommManager$register_comm_target(), comm$send(), msg$content, ...). The comms, their targets and the R functions
# they call are held by Elara (comm_r.cpp): a comm is its id with class "Comm", a received message a list of class
# "Message" (content, header, parent_header, metadata, buffers). What is here only passes the calls on.

.jv.comm.call <- function(routine, ...) .Call(routine, ..., PACKAGE = "(embedding)")

# A comm message's buffers: a list of raw vectors
.jv.comm.check_buffers <- function(buffers) {
  if (!is.list(buffers) || !all(vapply(buffers, is.raw, logical(1)))) stop("`buffers` must be a list of raw vectors", call. = FALSE)
}

#' Comm Manager class
#'
#' The comm manager, [CommManager], is the one object of this class.
#'
#' @rdname CommManager
.elara.CommManagerClass <- structure(list(), class = "CommManagerClass")

#' Comm manager
#'
#' Keeps the comm targets the kernel answers, and the comms open: `$register_comm_target(target_name, callback)`
#' (`callback(comm, message)` when the frontend opens a comm to it), `$unregister_comm_target(target_name)`,
#' `$new_comm(target_name, description)` (a comm the kernel opens, with `$open()`), `$comms()`,
#' `$target_callback(target_name)`, `$get_comm_info(target_name)`.
#'
.elara.CommManager <- .elara.CommManagerClass

.jv.s3.dollar.CommManagerClass <- function(x, name) {
  switch(name,
    register_comm_target = function(target_name, callback = function(comm, message) {}) {
      invisible(.jv.comm.call("elara_comm_register_target", target_name, callback))
    },
    unregister_comm_target = function(target_name) {
      invisible(.jv.comm.call("elara_comm_unregister_target", target_name))
    },
    new_comm = function(target_name, description = "") {
      .jv.comm.call("elara_comm_new", target_name, description)
    },
    comms = function() .jv.comm.call("elara_comm_list"),
    target_callback = function(target_name) .jv.comm.call("elara_comm_target_callback", target_name),
    get_comm_info = function(target_name = NULL) .jv.comm.call("CommManager__get_comm_info", target_name),
    print = function() print(x),
    NULL
  )
}

.jv.s3.print.CommManagerClass <- function(x, ...) {
  cat("<CommManager> ", length(x$comms()), " comms open\n", sep = "")
  invisible(x)
}

#' Comm class
#'
#' A comm: made by the kernel when the frontend opens one to a registered target, or by `Comm$new(target_name,
#' description)` (as `CommManager$new_comm()`). `$open(data, metadata, buffers)`, `$send(...)`, `$close(...)`,
#' `$on_message(handler)`, `$on_close(handler)` (`handler(message)`), and the fields `$id` and `$target_name`.
#'
.elara.Comm <- structure(list(new = function(target_name, description = "") .elara.CommManager$new_comm(target_name, description)),
  class = "hera_class")

.jv.s3.print.hera_class <- function(x, ...) {
  cat("<Comm> object generator\n")
  invisible(x)
}

.jv.s3.dollar.Comm <- function(x, name) {
  id <- unclass(x)
  message <- function(routine) {
    function(data = NULL, metadata = NULL, buffers = list()) {
      .jv.comm.check_buffers(buffers)
      invisible(.jv.comm.call(routine, id, .jv.json.prepare(data), .jv.json.prepare(metadata), buffers))
    }
  }
  switch(name,
    id = id,
    target_name = .jv.comm.call("elara_comm_target_name", id),
    open = message("elara_comm_open"),
    send = message("elara_comm_send"),
    close = message("elara_comm_close"),
    on_message = function(handler) invisible(.jv.comm.call("elara_comm_on_message", id, handler)),
    on_close = function(handler) invisible(.jv.comm.call("elara_comm_on_close", id, handler)),
    print = function() print(x),
    NULL
  )
}

.jv.s3.print.Comm <- function(x, ...) {
  description <- .jv.comm.call("elara_comm_description", unclass(x))
  target <- x$target_name
  if (is.null(target)) {
    writeLines(sprintf("<Comm id=%s (closed)>", unclass(x)))
  } else if (!length(description) || identical(description, "")) {
    writeLines(sprintf("<Comm id=%s target_name='%s'>", unclass(x), target))
  } else {
    writeLines(sprintf("<Comm id=%s target_name='%s' description='%s' >", unclass(x), target, description))
  }
  invisible(x)
}

# A horizontal rule with a title, as cli::rule() drew it.
.jv.comm.rule <- function(title) {
  width <- max(10L, getOption("width", 80L))
  paste0("-- ", title, " ", strrep("-", max(0L, width - nchar(title) - 4L)))
}

.jv.s3.print.Message <- function(x, ...) {
  for (field in c("content", "header", "parent_header", "metadata")) {
    writeLines(.jv.comm.rule(paste0("$", field)))
    utils::str(x[[field]])
  }
  invisible(x)
}
