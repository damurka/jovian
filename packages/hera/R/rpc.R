# The RPCs a frontend calls (Jovian's Session.listPackages(), listVariables(), helpServer(), ...), as Ark's .ps.rpc.*:
# Jovian calls .jv.rpc.call(method, args_json) -- the method's arguments as a JSON object, its result as JSON (printed
# as is: class "jv_json"). The methods are the .jv.rpc.* functions of packages.R, variables.R and help.R.

.jv.rpc.call <- function(method, args_json = "") {
  fn <- get0(paste0(".jv.rpc.", method), envir = .jv.NAMESPACE, mode = "function", inherits = FALSE)
  if (is.null(fn)) stop("no such method: ", method, call. = FALSE)
  args <- if (nzchar(args_json)) .jv.json.read(args_json) else list()
  .jv.rpc.result(do.call(fn, as.list(args)))
}

.jv.rpc.result <- function(x) {
  structure(.jv.json.write(x, null = "null"), class = "jv_json")
}

.jv.s3.print.jv_json <- function(x, ...) {
  cat(unclass(x), "\n", sep = "")
  invisible(x)
}

# The repositories to use: those given, then the session's (CRAN's cloud mirror when it has none set)
.jv.rpc.repos <- function(repos = NULL) {
  session <- getOption("repos")
  if (is.null(session) || identical(unname(session[["CRAN"]]), "@CRAN@")) session <- c(CRAN = "https://cloud.r-project.org")
  repos <- unlist(repos)
  unique(c(if (length(repos)) sub("/+$", "", repos), session))
}

# A data frame as rows: what the frontend gets as a list of objects
.jv.rpc.rows <- function(df) {
  df <- as.data.frame(df, stringsAsFactors = FALSE)
  rownames(df) <- NULL
  df
}
