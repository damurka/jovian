# Package management, for a frontend (Jovian's Session.listPackages(), installPackages(), ...), as Ark's .ps.rpc.pkg_*.
# A session loads no package of its own besides R's (see repl.R), so it can install or update any package, even on
# Windows where a loaded package's DLL cannot be replaced -- unless the user's code has loaded it.
#
# Jovian calls .jv.rpc.call(method, args_json): the method's arguments as a JSON object, its result as JSON (printed
# as is: class "jv_json").

.jv.rpc.call <- function(method, args_json = "") {
  fn <- get0(paste0(".jv.rpc.", method), envir = asNamespace("hera"), mode = "function", inherits = FALSE)
  if (is.null(fn)) stop("no such method: ", method, call. = FALSE)
  args <- if (nzchar(args_json)) .jv.json.read(args_json) else list()
  .jv.rpc.result(do.call(fn, as.list(args)))
}

.jv.rpc.result <- function(x) {
  structure(.jv.json.write(x, null = "null"), class = "jv_json")
}

#' @export
print.jv_json <- function(x, ...) {
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

# The packages installed in the session's libraries: name, version, library, and whether it is loaded or attached
.jv.rpc.pkg_list <- function() {
  ip <- utils::installed.packages(noCache = TRUE)
  first <- !duplicated(ip[, "Package"]) # a package in two libraries: the one R loads
  ip <- ip[first, , drop = FALSE]
  .jv.rpc.rows(data.frame(
    name = ip[, "Package"],
    version = ip[, "Version"],
    library = ip[, "LibPath"],
    priority = ifelse(is.na(ip[, "Priority"]), "", ip[, "Priority"]),
    loaded = ip[, "Package"] %in% loadedNamespaces(),
    attached = ip[, "Package"] %in% .packages(),
    stringsAsFactors = FALSE
  ))
}

# Whether the packages are installed, at least at the versions given (a named list, or NULL): one row each
.jv.rpc.is_installed <- function(packages, min_versions = NULL) {
  packages <- unlist(packages)
  rows <- lapply(packages, function(p) {
    version <- tryCatch(as.character(utils::packageVersion(p)), error = function(e) NA_character_)
    wanted <- if (!is.null(min_versions[[p]])) min_versions[[p]] else NA_character_
    ok <- !is.na(version) && (is.na(wanted) || package_version(version) >= package_version(wanted))
    data.frame(name = p, version = version, installed = ok, stringsAsFactors = FALSE)
  })
  if (!length(rows)) return(list())
  .jv.rpc.rows(do.call(rbind, rows))
}

# Installed packages with a newer version in the repositories. (old.packages() also lists a package whose version is
# the same but whose binary was built anew -- for a newer R, say: not an update to offer.)
.jv.rpc.pkg_outdated <- function(repos = NULL) {
  old <- suppressWarnings(utils::old.packages(repos = .jv.rpc.repos(repos)))
  if (is.null(old) || !nrow(old)) return(list())
  newer <- vapply(seq_len(nrow(old)), function(i) {
    tryCatch(package_version(old[i, "ReposVer"]) > package_version(old[i, "Installed"]), error = function(e) TRUE)
  }, logical(1))
  old <- old[newer, , drop = FALSE]
  if (!nrow(old)) return(list())
  .jv.rpc.rows(data.frame(
    name = old[, "Package"],
    installed = old[, "Installed"],
    available = old[, "ReposVer"],
    library = old[, "LibPath"],
    repository = old[, "Repository"],
    stringsAsFactors = FALSE
  ))
}

# Packages in the repositories whose name matches `query` (case-insensitive), at most `limit`
.jv.rpc.pkg_search <- function(query, repos = NULL, limit = 100L) {
  ap <- utils::available.packages(repos = .jv.rpc.repos(repos))
  hits <- ap[grepl(query, ap[, "Package"], ignore.case = TRUE, fixed = FALSE), , drop = FALSE]
  if (!nrow(hits)) return(list())
  exact <- tolower(hits[, "Package"]) == tolower(query)
  hits <- hits[order(!exact, hits[, "Package"]), , drop = FALSE][seq_len(min(nrow(hits), limit)), , drop = FALSE]
  .jv.rpc.rows(data.frame(name = hits[, "Package"], version = hits[, "Version"], repository = hits[, "Repository"],
    stringsAsFactors = FALSE))
}

# Installs the packages (and what they need) from the repositories into `lib` (the first library by default),
# printing what install.packages() prints -- the frontend gets it as it comes. The result: the version now
# installed of each, and those that could not be installed, with R's warnings.
.jv.rpc.install_packages <- function(packages, repos = NULL, lib = NULL) {
  packages <- unlist(packages)
  lib <- if (is.null(lib)) .libPaths()[[1L]] else lib
  problems <- character()
  withCallingHandlers(
    utils::install.packages(packages, lib = lib, repos = .jv.rpc.repos(repos)),
    warning = function(w) {
      problems <<- c(problems, conditionMessage(w))
      invokeRestart("muffleWarning")
    }
  )
  versions <- vapply(packages, function(p) {
    path <- find.package(p, lib.loc = lib, quiet = TRUE)
    if (length(path)) as.character(packageDescription(p, lib.loc = lib, fields = "Version")) else NA_character_
  }, "")
  list(
    installed = .jv.rpc.rows(data.frame(name = packages, version = unname(versions), stringsAsFactors = FALSE)),
    failed = I(packages[is.na(versions)]),
    warnings = I(problems)
  )
}

# R's own help server (tools::startDynamicHelp()), started if need be: its port and address. It answers while the
# session is idle (Elara services R's events then, as R's console does).
.jv.rpc.help_server <- function() {
  port <- tools::startDynamicHelp(NA)
  if (!port) port <- suppressMessages(tools::startDynamicHelp(TRUE))
  list(port = port, url = sprintf("http://127.0.0.1:%d", port))
}

# The help server's address for a help topic (in `package`, else wherever it is found), or NULL
.jv.rpc.help_url <- function(topic, package = NULL) {
  paths <- as.character(if (is.null(package)) utils::help(topic, help_type = "html") else utils::help(topic, package = (package), help_type = "html"))
  if (!length(paths)) return(NULL)
  server <- .jv.rpc.help_server()
  path <- paths[[1L]]
  sprintf("%s/library/%s/html/%s.html", server$url, basename(dirname(dirname(path))), basename(path))
}

# Removes the packages from the library they are installed in (the first one, if in several)
.jv.rpc.remove_packages <- function(packages, lib = NULL) {
  packages <- unlist(packages)
  removed <- character()
  for (p in packages) {
    path <- find.package(p, lib.loc = if (is.null(lib)) .libPaths() else lib, quiet = TRUE)
    if (!length(path)) next
    utils::remove.packages(p, lib = dirname(path[[1L]]))
    removed <- c(removed, p)
  }
  list(removed = I(removed))
}
