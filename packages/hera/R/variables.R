# The session's variables, for a frontend's variables pane and data viewer (Jovian's Session.listVariables(),
# readTable()), as Ark's variables comm: the objects of the global environment -- name, class, size, a short preview,
# and whether it is a table -- and a page of a table's rows as text, as R prints them. Called through .jv.rpc.call()
# (packages.R).

# At most this many objects are listed, and this many characters of a preview or a cell
.jv.vars.MAX_OBJECTS <- 5000L
.jv.vars.MAX_PREVIEW <- 200L
.jv.vars.MAX_CELL <- 1000L

.jv.rpc.var_list <- function() {
  env <- globalenv()
  names <- sort(ls(env))
  if (length(names) > .jv.vars.MAX_OBJECTS) names <- names[seq_len(.jv.vars.MAX_OBJECTS)]
  lapply(names, function(name) {
    # an active binding or a promise is not run for its value: only described
    if (bindingIsActive(name, env)) {
      return(list(name = name, type = "active binding", size = "", summary = "", table = FALSE))
    }
    x <- tryCatch(get(name, envir = env), error = function(e) NULL)
    list(name = name, type = class(x)[[1L]], size = .jv.vars.size(x), summary = .jv.vars.preview(x),
      table = .jv.vars.is_table(x))
  })
}

# A page of a table (a data frame, or a matrix): its columns (name, class), how many rows it has, and rows `start` to
# `start + count - 1` as text; row names when they are not just the row numbers
.jv.rpc.var_table <- function(name, start = 1L, count = 100L) {
  env <- globalenv()
  if (!exists(name, envir = env, inherits = FALSE)) stop("no object ", name, call. = FALSE)
  x <- get(name, envir = env)
  if (!.jv.vars.is_table(x)) stop(name, " is not a data frame or a matrix", call. = FALSE)

  n <- nrow(x)
  k <- ncol(x)
  names <- if (is.data.frame(x)) names(x) else colnames(x)
  if (is.null(names)) names <- paste0("V", seq_len(k))
  start <- max(1L, as.integer(start))
  last <- min(n, start + max(0L, as.integer(count)) - 1L)
  rows <- if (last >= start) start:last else integer()

  column <- function(j) if (is.data.frame(x)) x[[j]] else x[, j]
  columns <- lapply(seq_len(k), function(j) list(name = names[[j]], type = class(column(j))[[1L]]))
  cells <- lapply(seq_len(k), function(j) .jv.vars.format(.jv.vars.rows(column(j), rows)))
  page <- lapply(seq_along(rows), function(i) lapply(cells, `[[`, i))

  labels <- NULL
  has_names <- if (is.data.frame(x)) .row_names_info(x) > 0L else !is.null(rownames(x))
  if (has_names && length(rows)) labels <- as.list(as.character(rownames(x)[rows]))

  list(name = name, rowCount = n, columns = columns, start = start, count = length(rows), rowLabels = labels,
    rows = page)
}

# A table: a data frame (tibble, data.table) or a two-dimensional matrix
.jv.vars.is_table <- function(x) {
  is.data.frame(x) || (is.matrix(x) && length(dim(x)) == 2L)
}

# Rows `rows` of a column (a column may itself be a matrix or a data frame: its rows, each as one value)
.jv.vars.rows <- function(v, rows) {
  if (is.data.frame(v) || is.matrix(v)) {
    return(vapply(rows, function(i) paste(format(unlist(v[i, , drop = TRUE]), trim = TRUE), collapse = ", "), ""))
  }
  v[rows]
}

# Values as text, as R prints them: factors as their levels, dates as dates, NA as NA
.jv.vars.format <- function(v) {
  if (!length(v)) return(character())
  if (is.list(v)) {
    out <- vapply(v, function(e) if (is.null(e)) "NULL" else .jv.vars.preview(e), "")
  } else {
    if (is.factor(v)) v <- as.character(v)
    out <- tryCatch(format(v, trim = TRUE, justify = "none"), error = function(e) rep("?", length(v)))
    out[is.na(v)] <- "NA"
  }
  out <- as.character(out)
  long <- nchar(out, type = "chars", allowNA = TRUE) > .jv.vars.MAX_CELL
  long[is.na(long)] <- FALSE
  out[long] <- substr(out[long], 1L, .jv.vars.MAX_CELL)
  out
}

# How big: rows x columns for a table or an array, the length of a vector or a list, the objects of an environment
.jv.vars.size <- function(x) {
  tryCatch({
    d <- dim(x)
    if (length(d) >= 2L) return(paste(format(d, big.mark = ",", trim = TRUE), collapse = " × "))
    if (is.function(x) || is.null(x)) return("")
    if (is.environment(x)) return(paste(length(ls(x)), "objects"))
    format(length(x), big.mark = ",")
  }, error = function(e) "")
}

# What it holds, in one line
.jv.vars.preview <- function(x) {
  out <- tryCatch({
    if (is.data.frame(x)) {
      sprintf("%s obs. of %s variables", format(nrow(x), big.mark = ","), ncol(x))
    } else if (is.function(x)) {
      paste0("function(", paste(names(formals(args(x))), collapse = ", "), ")")
    } else if (is.environment(x)) {
      format(x)
    } else if (is.atomic(x) && length(x)) {
      v <- utils::head(as.vector(if (is.factor(x)) as.character(x) else x), 20L)
      text <- format(v, trim = TRUE)
      text[is.na(v)] <- "NA"
      if (is.character(v)) text <- ifelse(is.na(v), "NA", paste0("\"", v, "\""))
      paste(text, collapse = " ")
    } else if (is.list(x)) {
      sprintf("List of %d", length(x))
    } else {
      paste(utils::capture.output(utils::str(x, max.level = 0L, give.attr = FALSE)), collapse = " ")
    }
  }, error = function(e) "")
  out <- paste(out, collapse = " ")
  if (nchar(out, allowNA = TRUE) > .jv.vars.MAX_PREVIEW) out <- paste0(substr(out, 1L, .jv.vars.MAX_PREVIEW), "...")
  out
}
