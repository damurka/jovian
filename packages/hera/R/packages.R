# Package management, for a frontend's packages pane (Jovian's Session.listPackages(), outdatedPackages(), ...), as
# Ark's .ps.rpc.pkg_*. Called through .jv.rpc.call() (rpc.R). Installing is not here: Session.installPackages() goes
# through the session manager's ensureRPackage(), which installs in a packages session of its own (packages-ensure.R).

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
