// Installs or updates an R package and what its dependency tree needs, in a
// separate Rscript process -- never in a session's kernel, which may have the
// very packages being replaced loaded (on Windows a loaded package's DLL cannot
// be replaced). What an application decides -- the oldest R it supports, which
// library its packages go into -- it passes in.
import { spawn } from 'node:child_process';
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { delimiter, join } from 'node:path';
import { createInterface } from 'node:readline';
import { setTimeout as sleep } from 'node:timers/promises';
import { findRscript } from './runtimes.js';

/** What to install: {@link ensureRPackage}'s request. */
export interface RPackageRequest {
    name: string;
    /** The oldest version that will do; installed or updated when what is there is older. */
    minVersion?: string;
    /** Repositories to look in before CRAN, e.g. an r-universe (`https://<owner>.r-universe.dev`). */
    repos?: readonly string[];
    /**
     * Also bring the package, and the dependencies that come from `repos`, up to the newest version available.
     * Without it nothing is downloaded when `minVersion` is already met.
     */
    update?: boolean;
    /**
     * Also install the suggested packages of the packages from `repos` (what their code uses when it is there).
     * Default true. Without them a fresh install finishes much sooner, so they can be left for a second run.
     */
    optional?: boolean;
}

/**
 * How far an install has got. `waiting`: another process is installing into the same library; `checking`: looking
 * in the repos for what is needed; `downloading` the `total` packages; `installing` them, `done` so far;
 * `retrying` downloads that broke off.
 */
export interface RPackageProgress {
    phase: 'waiting' | 'checking' | 'downloading' | 'installing' | 'retrying';
    done?: number;
    total?: number;
    /** The package being installed, or the last one installed. */
    current?: string;
    /** While `downloading` or `retrying`: how much of the packages has arrived so far, in bytes. */
    bytes?: number;
}

export interface RPackageResult {
    /** The version installed before, if any. */
    previousVersion?: string;
    /** The version installed now. */
    version?: string;
    /** The packages this run installed or updated. */
    installed: string[];
    /** A repo could not be reached and what is installed was kept, unchecked against the repos. */
    offline: boolean;
}

/** The name of the error ensureRPackage() rejects with when the repos can't be reached and what is installed won't do. */
export const R_PACKAGES_OFFLINE = 'RPackagesOffline';

export interface RPackageOptions {
    /** The R installation (its Rscript runs the install). */
    rHome: string;
    /**
     * The libraries R sees, in order (R_LIBS): packages are installed into the first. Without, R's own
     * (.libPaths()[1], usually the user library).
     */
    libraries?: readonly string[];
    /** The oldest R the packages may be installed for; an older R fails with a message saying so. */
    minRVersion?: string;
    /**
     * A DESCRIPTION field (e.g. "Config/myapp/onDemand") in which a package from `repos` names suggested packages
     * to leave out: ones its users install only when they ask for the feature that needs them.
     */
    onDemandField?: string;
    /** The install's progress lines, for a log. */
    onOutput?: (line: string) => void;
    /** How far the install has got, for people. */
    onProgress?: (progress: RPackageProgress) => void;
    /** Default 30 minutes. */
    timeoutMs?: number;
}

/**
 * Installs or updates one R package, and whatever of its dependency tree it
 * needs, into the first library on `.libPaths()` (R_LIBS, set by the caller). Run by Rscript with the arguments
 * `<package> <minVersion or ""> <update 0/1> <optional 0/1> <repo>...`.
 *
 * Without `update`, nothing is downloaded when the package is already
 * installed at `minVersion` or newer, so it is cheap to run before every
 * launch. With `update`, the package and every dependency that comes from the
 * app's own repos (not CRAN) are brought up to the newest available version.
 * In both modes a dependency that is missing, or older than a version some
 * package in the tree requires (e.g. `mylib (>= 1.1.0)`), is installed --
 * and, whenever it goes to the repos and `optional` is 1, the suggested
 * packages of the app's own packages too (best effort: see the comment in the
 * script).
 *
 * Talks back on stdout: `JOVIAN_PKG: <progress>`, `JOVIAN_PKG_ERROR: <why>`,
 * `JOVIAN_PKG_OFFLINE: <why>` (a repo could not be reached and what is
 * installed won't do) and, on success,
 * `JOVIAN_PKG_RESULT: <before> <after> <installed,...> <online|offline>`.
 * Reads from the environment: JOVIAN_PKG_MIN_R (the oldest R allowed),
 * JOVIAN_PKG_ON_DEMAND_FIELD (see RPackageOptions.onDemandField) and
 * JOVIAN_PKG_DOWNLOADS (where packages are downloaded, watched for progress).
 */
const R_INSTALL_SCRIPT = `
local({
    say <- function(...) writeLines(paste0("JOVIAN_PKG: ", ...))
    fail <- function(msg) {
        writeLines(paste0("JOVIAN_PKG_ERROR: ", gsub("[[:cntrl:]]+", " ", paste(msg, collapse = " "))))
        quit(save = "no", status = 1)
    }
    args <- commandArgs(trailingOnly = TRUE)
    pkg <- args[[1]]
    min_version <- args[[2]]
    update <- identical(args[[3]], "1")
    with_optional <- !identical(args[[4]], "0")
    app_repos <- sub("/+$", "", args[-(1:4)])

    min_r <- Sys.getenv("JOVIAN_PKG_MIN_R")
    if (nzchar(min_r) && getRversion() < min_r) {
        fail(sprintf("R %s is too old: R %s or newer is needed.", getRversion(), min_r))
    }
    lib <- .libPaths()[[1]]
    if (file.access(lib, 2) != 0) fail(sprintf("The R library %s is not writable.", lib))

    version_of <- function(p) {
        tryCatch(as.character(utils::packageVersion(p, lib.loc = .libPaths())), error = function(e) NA_character_)
    }
    meets <- function(v, req) !is.na(v) && (is.na(req) || !nzchar(req) || package_version(v) >= package_version(req))
    report <- function(before, installed, reached = TRUE) {
        writeLines(paste("JOVIAN_PKG_RESULT:", before, version_of(pkg), if (length(installed)) paste(installed, collapse = ",") else "-",
            if (reached) "online" else "offline"))
    }

    base_packages <-rownames(utils::installed.packages(priority = "base"))
    # "a (>= 1.0), b" -> list(c("a", "1.0"), c("b", NA))
    requirements <- function(deps) {
        if (is.na(deps)) return(list())
        lapply(trimws(strsplit(deps, ",")[[1]]), function(d) {
            m <- regmatches(d, regexec("^([[:alnum:].]+)[[:space:]]*([(][[:space:]]*>=?[[:space:]]*([^)[:space:]]+))?", d))[[1]]
            if (length(m) == 4 && nzchar(m[[2]])) c(m[[2]], if (nzchar(m[[4]])) m[[4]] else NA) else c("", NA)
        })
    }

    # R loads the first copy of a package on .libPaths(), wherever it is -- the caller's library comes first, but a
    # package only in a later one (the user's own library) is used from there. What that copy requires is read from
    # the copy itself, not the repo index: a local build can carry a release's version number and still require more
    # than the release does, and then R stops half way through loading the app
    # ("namespace 'x' 0.3.4 is being loaded, but >= 0.4.0 is required"). One row per requirement R would refuse,
    # including a package that is not installed at all (has is NA): an install that stopped half way leaves the
    # app's own package in place without some of what it loads.
    conflicts <- function() {
        found <- data.frame(by = character(), by_version = character(), by_lib = character(),
            dep = character(), needs = character(), has = character(), has_lib = character())
        seen <- character()
        queue <- pkg
        while (length(queue)) {
            p <- queue[[1]]
            queue <- queue[-1]
            if (p %in% c(seen, base_packages, "R")) next
            seen <- c(seen, p)
            path <- find.package(p, lib.loc = .libPaths(), quiet = TRUE)
            if (!length(path)) next
            # LinkingTo is only for building: R checks Depends and Imports when it loads a package
            desc <- read.dcf(file.path(path[[1]], "DESCRIPTION"), fields = c("Version", "Depends", "Imports"))
            for (field in c("Depends", "Imports")) for (r in requirements(desc[1, field])) {
                if (!nzchar(r[[1]]) || r[[1]] %in% c(base_packages, "R")) next
                queue <- c(queue, r[[1]])
                has <- version_of(r[[1]])
                if (is.na(has)) {
                    found[nrow(found) + 1, ] <- c(p, desc[1, "Version"], dirname(path[[1]]), r[[1]], r[[2]], NA, NA)
                } else if (!is.na(r[[2]]) && package_version(has) < package_version(r[[2]])) {
                    found[nrow(found) + 1, ] <- c(p, desc[1, "Version"], dirname(path[[1]]), r[[1]], r[[2]], has,
                        dirname(find.package(r[[1]], lib.loc = .libPaths(), quiet = TRUE)[[1]]))
                }
            }
        }
        found
    }
    describe <- function(cf) {
        ifelse(is.na(cf$has),
            sprintf("%s %s (in %s) needs %s, which is not installed.", cf$by, cf$by_version, cf$by_lib, cf$dep),
            sprintf("%s %s (in %s) needs %s %s or newer, but R would load %s %s from %s.",
                cf$by, cf$by_version, cf$by_lib, cf$dep, cf$needs, cf$dep, cf$has, cf$has_lib))
    }

    before <- version_of(pkg)
    clashes <- conflicts()
    if (!update && meets(before, min_version) && !nrow(clashes)) {
        report(before, character())
        quit(save = "no", status = 0)
    }

    repos_option <- getOption("repos")
    cran <- if ("CRAN" %in% names(repos_option)) repos_option[["CRAN"]] else NA
    if (is.na(cran) || cran == "@CRAN@") cran <- "https://cloud.r-project.org"

    # On Linux CRAN has source packages only: building them needs the distribution's -dev libraries and, for the
    # modelling packages, 20 minutes or more. Posit Package Manager has them prebuilt for Ubuntu and Debian, so CRAN
    # comes from there when the distribution is one it builds for (and the user has not chosen a mirror of their own).
    # It tells an R that wants binaries by the user agent.
    linux <- NULL
    if (R.version$os == "linux-gnu" && file.exists("/etc/os-release")) {
        os_release <- readLines("/etc/os-release", warn = FALSE)
        field <- function(key) {
            line <- grep(paste0("^", key, "="), os_release, value = TRUE)
            if (length(line)) gsub('"', "", sub("^[^=]*=", "", line[[1]])) else ""
        }
        linux <- list(id = field("ID"), version = field("VERSION_ID"), codename = field("VERSION_CODENAME"))
        if (!linux$id %in% c("ubuntu", "debian")) linux <- NULL
    }
    if (!is.null(linux) && nzchar(linux$codename) && sub("/+$", "", cran) == "https://cloud.r-project.org") {
        options(HTTPUserAgent = sprintf("R/%s R (%s)", getRversion(), paste(getRversion(), R.version$platform, R.version$arch, R.version$os)))
        binaries <- paste0("https://p3m.dev/cran/__linux__/", linux$codename, "/latest")
        if (tryCatch(length(suppressWarnings(readLines(paste0(binaries, "/src/contrib/PACKAGES"), n = 1))) > 0, error = function(e) FALSE)) {
            cran <- binaries
        }
    }

    # r-universe builds its packages for the current Ubuntu LTS as well: on Ubuntu the app's own packages come prebuilt
    # from there (the Bayesian model's would otherwise compile Stan here, for many minutes), with the source repo after
    # it for any package not built yet (the first entry for a package wins).
    if (!is.null(linux) && linux$id == "ubuntu" && nzchar(linux$codename)) {
        r_minor <- paste(R.version$major, sub("[.].*$", "", R.version$minor), sep = ".")
        arch <- if (R.version$arch %in% c("aarch64", "arm64")) "aarch64" else "x86_64"
        app_repos <- unlist(lapply(app_repos, function(repo) {
            if (!grepl("[.]r-universe[.]dev$", repo)) return(repo)
            bin <- paste0(repo, "/bin/linux/", linux$codename, "-", arch, "/", r_minor)
            found <- tryCatch(length(suppressWarnings(readLines(paste0(bin, "/src/contrib/PACKAGES"), n = 1))) > 0, error = function(e) FALSE)
            if (found) c(bin, repo) else repo
        }))
    }

    repos <- c(app_repos, CRAN = cran)
    # R gives up on a download after 60 seconds, which the larger packages (V8, BH, StanHeaders: tens of MB) take
    # longer than on a slow connection -- and then everything that needs them fails too
    options(repos = repos, timeout = max(3600, getOption("timeout")))

    # The Ubuntu/Debian packages (from Posit's list of R packages' system requirements) that the given R packages
    # need and that are not installed. A prebuilt package still needs the libraries it links to, and one built from
    # source needs their -dev packages as well. Empty when that cannot be told (another distribution, no dpkg, offline).
    missing_system <- function(pkgs) {
        # only CRAN's packages: the service knows no others, and answers an error when asked about one
        pkgs <- pkgs[startsWith(db[pkgs, "Repository"], cran)]
        if (is.null(linux) || !length(pkgs) || !nzchar(Sys.which("dpkg-query"))) return(character())
        release <- if (linux$id == "ubuntu") linux$version else sub("[.].*$", "", linux$version)
        query <- paste0("https://p3m.dev/__api__/repos/cran/sysreqs?all=false&distribution=", linux$id, "&release=", release,
            paste0("&pkgname=", pkgs, collapse = ""))
        body <- tryCatch(paste(suppressWarnings(readLines(query, warn = FALSE)), collapse = ""), error = function(e) "")
        lists <- regmatches(body, gregexpr('"packages":[[][^]]*[]]', body))[[1]]
        needed <- unique(trimws(unlist(strsplit(gsub('"packages":|[][]|"', "", lists), ","))))
        needed <- needed[nzchar(needed)]
        # every installed package and the names it provides: a name in the list can be one that a newer package took
        # over (libfreetype6-dev is provided by libfreetype-dev). system2() runs a shell, so the format is quoted.
        format <- paste0("-f=$", "{db:Status-Abbrev}|$", "{Package}|$", "{Provides};")
        rows <- strsplit(paste(suppressWarnings(system2("dpkg-query", c("-W", shQuote(format)), stdout = TRUE, stderr = FALSE)), collapse = ""), ";")[[1]]
        rows <- strsplit(rows[startsWith(rows, "ii")], "|", fixed = TRUE)
        present <- unique(trimws(sub("[(].*$", "", unlist(lapply(rows, function(r) c(r[2], if (length(r) > 2) strsplit(r[3], ",")[[1]]))))))
        setdiff(needed, present)
    }

    say("Checking ", paste(repos, collapse = ", "))
    # An unreachable repo (offline, proxy, repo down) is only a warning to available.packages(), which then lists
    # nothing from it -- so it is noticed here rather than mistaken for "the package doesn't exist".
    index_failed <- FALSE
    db <- withCallingHandlers(
        tryCatch(utils::available.packages(repos = repos, type = "source"), error = function(e) fail(conditionMessage(e))),
        warning = function(w) {
            if (startsWith(conditionMessage(w), "unable to access index")) index_failed <<- TRUE
            invokeRestart("muffleWarning")
        }
    )
    if (index_failed) {
        unreachable <- repos[!vapply(repos, function(r) any(startsWith(db[, "Repository"], r)), NA)]
        # the installed version will do: keep using it, and look for updates next time
        if (meets(before, min_version) && !nrow(clashes)) {
            report(before, character(), reached = FALSE)
            quit(save = "no", status = 0)
        }
        writeLines(paste0("JOVIAN_PKG_OFFLINE: Could not reach ", paste(unreachable, collapse = ", "), ".",
            if (nrow(clashes)) paste0(" ", paste(describe(clashes), collapse = " "))))
        quit(save = "no", status = 1)
    }
    if (!pkg %in% rownames(db)) fail(sprintf("%s was not found in %s.", pkg, paste(repos, collapse = ", ")))

    # a requirement no version in the repos meets: installing cannot help
    stuck <- clashes[!clashes$dep %in% rownames(db), ]
    if (nrow(stuck)) fail(c(describe(stuck), sprintf("%s is not in %s.", stuck$dep, paste(repos, collapse = ", "))))

    # LinkingTo packages are C/C++ headers (BH, RcppEigen, StanHeaders: hundreds of MB) needed only to compile a
    # package, not to install or run a prebuilt one -- and Windows and macOS get prebuilt packages, as does Linux from
    # Posit and r-universe's Linux builds. So they are followed only for a package that is compiled here.
    built_here <- function(p) {
        if (.Platform$pkgType != "source") return(FALSE)
        repo <- db[p, "Repository"]
        identical(unname(db[p, "NeedsCompilation"]), "yes") && !startsWith(repo, "https://p3m.dev/") && !grepl("/bin/linux/", repo, fixed = TRUE)
    }
    strong_for <- function(p) c("Depends", "Imports", if (built_here(p)) "LinkingTo")
    # the packages these need, recursively, as far as the repos have them
    dependencies_of <- function(pkgs) {
        seen <- character()
        queue <- intersect(pkgs, rownames(db))
        while (length(queue)) {
            p <- queue[[1]]
            queue <- queue[-1]
            if (p %in% seen) next
            seen <- c(seen, p)
            deps <- unlist(lapply(strong_for(p), function(field) vapply(requirements(db[p, field]), function(r) r[[1]], "")))
            queue <- c(queue, setdiff(intersect(deps, rownames(db)), seen))
        }
        seen
    }

    tree <- unique(c(pkg, clashes$dep, dependencies_of(c(pkg, clashes$dep))))
    tree <- intersect(setdiff(tree, base_packages), rownames(db))

    # the highest version any package in the tree requires of each dependency -- by the repo index, and by the
    # installed copies R would load (see conflicts())
    required <- stats::setNames(rep(NA_character_, length(tree)), tree)
    if (nzchar(min_version)) required[[pkg]] <- min_version
    raise <- function(dep, version) {
        if (!is.na(version) && dep %in% tree && (is.na(required[[dep]]) || package_version(version) > package_version(required[[dep]]))) {
            required[[dep]] <<- version
        }
    }
    for (p in tree) for (field in strong_for(p)) for (r in requirements(db[p, field])) raise(r[[1]], r[[2]])
    for (i in seq_len(nrow(clashes))) raise(clashes$dep[[i]], clashes$needs[[i]])
    if (nrow(clashes)) say(paste(describe(clashes), collapse = " "))

    own <- rownames(db)[vapply(db[, "Repository"], function(r) any(startsWith(r, app_repos)), NA)]

    # Suggested packages of the app's own packages (from its repos, not CRAN's): their code uses them at run time
    # through requireNamespace() -- PDF export, editable PowerPoint charts, pictures -- and quietly does less, or
    # stops, without them. Best effort: tools only needed to develop a package are left out, and one that can't be
    # found or installed is logged, never a reason to fail.
    dev_only <- c("testthat", "knitr", "rmarkdown", "spelling", "covr", "lintr", "roxygen2", "devtools", "usethis", "pkgdown")
    suggested <- unique(unlist(lapply(intersect(tree, c(pkg, own)), function(p) {
        s <- db[p, "Suggests"]
        if (is.na(s)) character() else trimws(sub("[(].*$", "", strsplit(s, ",")[[1]]))
    })))
    suggested <- setdiff(suggested, c(tree, dev_only, "", rownames(utils::installed.packages(priority = "base"))))
    # An app's package can name suggested packages to install only when the app asks for them -- a feature not
    # everyone uses, with a large download (a Bayesian model) -- in a DESCRIPTION field the caller names
    # (JOVIAN_PKG_ON_DEMAND_FIELD, e.g. "Config/myapp/onDemand: pkgA, pkgB"). R itself ignores Config/ fields. Read
    # from the installed copies (repo indexes don't carry it): this pass runs after the app's own packages are installed.
    on_demand_field <- Sys.getenv("JOVIAN_PKG_ON_DEMAND_FIELD")
    on_demand <- if (!nzchar(on_demand_field)) character() else unique(unlist(lapply(intersect(tree, c(pkg, own)), function(p) {
        value <- suppressWarnings(tryCatch(utils::packageDescription(p, lib.loc = .libPaths(), fields = on_demand_field),
            error = function(e) NA))
        if (length(value) != 1 || is.na(value)) character() else trimws(strsplit(value, ",")[[1]])
    })))
    suggested <- setdiff(suggested, on_demand)
    if (!with_optional) suggested <- character()
    not_found <- setdiff(suggested, rownames(db))
    if (length(not_found)) say("Optional packages not in the repos, skipped: ", paste(not_found, collapse = ", "))
    optional <- intersect(suggested, rownames(db))
    optional <- unique(c(optional, dependencies_of(optional)))
    optional <- setdiff(intersect(optional, rownames(db)), c(tree, rownames(utils::installed.packages(priority = "base"))))
    optional_todo <- optional[is.na(vapply(optional, version_of, ""))]

    current <- vapply(tree, version_of, "")
    todo <- tree[!mapply(meets, current, required[tree])]
    if (update) {
        newer <- tree %in% c(pkg, own) & !is.na(current) &
            mapply(function(a, b) is.na(b) || package_version(a) > package_version(b), db[tree, "Version"], current)
        todo <- union(todo, tree[newer])
    }
    if (!length(todo) && !length(optional_todo)) {
        report(before, character())
        quit(save = "no", status = 0)
    }

    unavailable <- todo[!mapply(meets, db[todo, "Version"], required[todo])]
    if (length(unavailable)) {
        fail(c(describe(clashes[clashes$dep %in% unavailable, ]),
            sprintf("%s %s is needed, but the newest available is %s.", unavailable, required[unavailable], db[unavailable, "Version"])))
    }

    # missing system libraries would make the install fail package by package, minutes later: say what to install now
    lacking <- missing_system(todo)
    if (length(lacking)) {
        fail(sprintf("%s needs system libraries that are not installed. Install them in a terminal with: sudo apt install %s",
            pkg, paste(lacking, collapse = " ")))
    }
    if (length(optional_todo)) {
        lacking_optional <- missing_system(optional_todo)
        if (length(lacking_optional)) {
            say("Some optional packages need system libraries that are not installed (sudo apt install ",
                paste(lacking_optional, collapse = " "), ")")
        }
    }

    say("Installing ", paste(c(todo, optional_todo), collapse = ", "))
    outputs <- file.path(tempdir(), "install-outputs")
    dir.create(outputs, showWarnings = FALSE)
    # the packages are downloaded into the caller's folder when it gives one, so it can show how much has arrived
    # (R downloads them all at once and says nothing until every one is done)
    downloads <- Sys.getenv("JOVIAN_PKG_DOWNLOADS")
    downloads <- if (nzchar(downloads) && dir.exists(downloads)) downloads else NULL
    # problems: the warnings of the latest install.packages() call
    problems <- character()
    install_now <- function(pkgs) {
        problems <<- character()
        withCallingHandlers(
            utils::install.packages(pkgs, lib = lib, repos = repos, dependencies = FALSE, destdir = downloads,
                keep_outputs = outputs, Ncpus = max(1L, parallel::detectCores() - 1L, na.rm = TRUE)),
            warning = function(w) {
                problems <<- c(problems, conditionMessage(w))
                invokeRestart("muffleWarning")
            }
        )
    }
    unfinished <- function() {
        c(todo[vapply(todo, function(p) !meets(version_of(p), required[[p]]), NA)],
            optional_todo[is.na(vapply(optional_todo, version_of, ""))])
    }
    install_now(c(todo, optional_todo))
    # A connection that drops (common on slow or shared lines) loses the downloads under way, and with them every
    # package that needs one: try what did not finish twice more before giving up.
    for (attempt in 1:2) {
        left_over <- unfinished()
        if (!length(left_over) || !any(grepl("download of package|cannot open URL|Could not connect", problems))) break
        say("Some downloads did not finish; trying again: ", paste(left_over, collapse = ", "))
        Sys.sleep(5)
        install_now(left_over)
    }
    # whether these packages failed because their download did (as opposed to their build)
    downloads_failed <- function(pkgs) {
        lost <- grep("download of package|cannot open URL|Could not connect", problems, value = TRUE)
        vapply(pkgs, function(p) {
            any(grepl(paste0("(^|[^[:alnum:].])", gsub(".", "[.]", p, fixed = TRUE), "([^[:alnum:]._]|_[0-9]|$)"), lost))
        }, NA)
    }
    # why a package did not build, from R CMD INSTALL's output: the compiler's or configure's first error, or NA
    # when it only failed because a package it needs did
    build_error <- function(p) {
        out <- file.path(outputs, paste0(p, ".out"))
        if (!file.exists(out)) return(NA_character_)
        lines <- readLines(out, warn = FALSE)
        if (any(grepl("^ERROR: dependenc", lines))) return(NA_character_)
        hit <- c(grep("fatal error", lines, value = TRUE), grep("^ERROR:|error:", lines, value = TRUE))
        if (length(hit)) paste0(p, ": ", trimws(hit[[1]])) else NA_character_
    }
    optional_failed <- optional_todo[is.na(vapply(optional_todo, version_of, ""))]
    lost_connection <- any(downloads_failed(optional_failed))
    if (length(optional_failed)) say("Optional packages that could not be installed: ", paste(optional_failed, collapse = ", "))
    after <- vapply(todo, version_of, "")
    failed <- todo[!mapply(meets, after, required[todo])]
    # what the installed packages need of each other, now that some were replaced
    left <- if (length(failed)) clashes[0, ] else conflicts()
    if (nrow(left)) fail(describe(left))
    if (!length(failed)) {
        # an optional package lost to a broken connection is tried again once back online
        report(before, c(setdiff(todo, failed), setdiff(optional_todo, optional_failed)), reached = !(length(optional_failed) && lost_connection))
        quit(save = "no", status = 0)
    }
    if (length(failed)) {
        # offline only when a package we need could not be downloaded; one that downloaded and did not build is an error.
        # Either way the app cannot run: a failed package is one whose installed version (install.packages() keeps the
        # previous one of anything it could not replace) does not meet what the tree requires -- so not "keep using
        # what is installed", even when the app's own package is there.
        lost_connection <- any(downloads_failed(failed))
        if (lost_connection) {
            writeLines(paste0("JOVIAN_PKG_OFFLINE: The download of ", paste(failed[downloads_failed(failed)], collapse = ", "), " did not finish."))
            quit(save = "no", status = 1)
        }
        reasons <- stats::na.omit(vapply(failed, build_error, ""))
        fail(c(sprintf("Could not install %s.", paste(failed, collapse = ", ")),
            if (length(reasons)) paste(reasons, collapse = " ") else unique(problems)))
    }
})
`;


const R_PACKAGE = `([A-Za-z][A-Za-z0-9.]*)`;
/** R's lines saying a package is installed: a Windows binary unpacked, a source (or Linux binary) package built. */
const R_INSTALLED = [new RegExp(`^package [\u2018'"]${R_PACKAGE}[\u2019'"] successfully unpacked`), new RegExp(`^\\* DONE \\(${R_PACKAGE}\\)`)];
/** R's lines saying a package is being built. */
const R_BUILDING = [new RegExp(`^begin installing package [\u2018'"]${R_PACKAGE}[\u2019'"]`), new RegExp(`^\\* installing \\*(?:source|binary)\\* package [\u2018'"]${R_PACKAGE}[\u2019'"]`)];

/**
 * Follows an install's output -- {@link R_INSTALL_SCRIPT}'s progress (without its `JOVIAN_PKG: `) and the lines
 * install.packages() prints per package -- into how far it has got. Returns the function to give each line to.
 */
export function followRInstall(onStatus: (status: RPackageProgress) => void): (line: string, fromScript: boolean) => void {
    let total = 0;
    const installed = new Set<string>();
    return (line, fromScript) => {
        if (fromScript) {
            if (line.startsWith('Checking ')) {
                onStatus({ phase: 'checking' });
            } else if (line.startsWith('Installing ')) {
                total = line.slice('Installing '.length).split(', ').length;
                onStatus({ phase: 'downloading', done: 0, total });
            } else if (line.startsWith('Some downloads did not finish')) {
                onStatus({ phase: 'retrying', done: installed.size, total });
            }
            return;
        }
        for (const pattern of R_INSTALLED) {
            const match = pattern.exec(line);
            if (match && !installed.has(match[1])) {
                installed.add(match[1]);
                onStatus({ phase: 'installing', done: Math.min(installed.size, total), total, current: match[1] });
                return;
            }
        }
        for (const pattern of R_BUILDING) {
            const match = pattern.exec(line);
            if (match) {
                onStatus({ phase: 'installing', done: Math.min(installed.size, total), total, current: match[1] });
                return;
            }
        }
    };
}

/** The lock file in a library while a process installs into it (see {@link lockLibrary}). */
export const LIBRARY_LOCK_FILE = '.jovian-install.lock';

/** Whether a process is running: one of another user (an elevated application) counts too. */
function isRunning(pid: number): boolean {
    try {
        process.kill(pid, 0);
        return true;
    } catch (error) {
        return (error as NodeJS.ErrnoException).code === 'EPERM';
    }
}

/**
 * Takes the library for one install, across processes. Within one, installs should run one at a time; but two
 * applications can run at once (one elevated, one not, can't see each other) and both install into the same library
 * -- and R locks the whole library while it unpacks a Windows package, so the second failed half way ("failed to lock
 * directory ... 00LOCK"). The lock file holds its owner's pid; one whose process is gone, or older than `staleMs`, is
 * a crashed install's and is taken over. Resolves with the function that gives the library back.
 */
export async function lockLibrary(library: string, staleMs: number, onWait: () => void): Promise<() => Promise<void>> {
    await fs.promises.mkdir(library, { recursive: true });
    const lockFile = join(library, LIBRARY_LOCK_FILE);
    let waited = false;
    for (; ;) {
        try {
            await fs.promises.writeFile(lockFile, JSON.stringify({ pid: process.pid, at: Date.now() }), { flag: 'wx' });
            return () => fs.promises.rm(lockFile, { force: true });
        } catch (error) {
            if ((error as NodeJS.ErrnoException).code !== 'EEXIST') {
                throw error;
            }
        }
        let holder: { pid?: unknown; at?: unknown } | undefined;
        try {
            holder = JSON.parse(await fs.promises.readFile(lockFile, 'utf8'));
        } catch {
            // just created and not written yet, or gone since: look again
        }
        const stat = await fs.promises.stat(lockFile).catch(() => undefined);
        const age = Date.now() - (typeof holder?.at === 'number' ? holder.at : stat?.mtimeMs ?? Date.now());
        const gone = typeof holder?.pid === 'number' && !isRunning(holder.pid);
        if (stat && (gone || age > staleMs)) {
            await fs.promises.rm(lockFile, { force: true });
            continue;
        }
        if (!waited) {
            waited = true;
            onWait();
        }
        await sleep(2000);
    }
}

/**
 * R's own locks left by an install that stopped half way (the application closed during it, a crash): `00LOCK` (the whole
 * library) and `00LOCK-<package>`. R refuses to install while they are there, and says to remove them. Called with
 * the library taken (lockLibrary), so no other install is running to own them.
 */
async function removeStaleRLocks(library: string): Promise<void> {
    const names = await fs.promises.readdir(library).catch(() => [] as string[]);
    await Promise.all(names.filter(name => name === '00LOCK' || name.startsWith('00LOCK-'))
        .map(name => fs.promises.rm(join(library, name), { recursive: true, force: true })));
}

/**
 * Installs or updates an R package, and whatever of its dependency tree it needs (see {@link R_INSTALL_SCRIPT}), into
 * the first of `options.libraries`, in its own Rscript process -- never in a session's kernel, which may have the very
 * packages being replaced loaded. One install at a time per library, across processes ({@link lockLibrary}). Rejects
 * with R's own explanation when the install fails, and with an error named {@link R_PACKAGES_OFFLINE} when the repos
 * can't be reached and what is installed won't do.
 */
export async function ensureRPackage(request: RPackageRequest, options: RPackageOptions): Promise<RPackageResult> {
    const rscript = findRscript(options.rHome);
    if (!rscript) {
        throw new Error(`There is no Rscript in ${options.rHome}`);
    }
    const rLibs = options.libraries?.filter(library => library.length > 0).join(delimiter) || undefined;
    const timeoutMs = options.timeoutMs ?? 30 * 60 * 1000;
    // the library packages go into: R_LIBS' first
    const library = options.libraries?.find(path => path.length > 0);
    const unlock = library
        ? await lockLibrary(library, timeoutMs + 5 * 60 * 1000, () => {
            options.onOutput?.('Waiting for another process to finish installing R packages into the library');
            options.onProgress?.({ phase: 'waiting' });
        })
        : undefined;
    let directory: string | undefined;
    try {
        if (library) {
            await removeStaleRLocks(library);
        }
        directory = await fs.promises.mkdtemp(join(tmpdir(), 'datasuite-r-install-'));
        const scriptFile = join(directory, 'install.R');
        await fs.promises.writeFile(scriptFile, R_INSTALL_SCRIPT);
        const args = [scriptFile, request.name, request.minVersion ?? '', request.update ? '1' : '0', request.optional === false ? '0' : '1', ...(request.repos ?? [])];
        // where R downloads the packages (the script's destdir), watched to show how much has arrived
        const downloads = join(directory, 'downloads');
        await fs.promises.mkdir(downloads);
        const env = {
            ...process.env,
            JOVIAN_PKG_DOWNLOADS: downloads,
            JOVIAN_PKG_MIN_R: options.minRVersion ?? '',
            JOVIAN_PKG_ON_DEMAND_FIELD: options.onDemandField ?? '',
            ...(rLibs ? { R_LIBS: rLibs } : {})
        };

        let failure: string | undefined;
        let offline: string | undefined;
        let result: RPackageResult | undefined;
        // R's own output: what install.packages() prints per package (followed for onProgress), compiler output, errors
        const otherOutput: string[] = [];
        let status: RPackageProgress | undefined;
        const follow = followRInstall(next => {
            status = next;
            options.onProgress?.(next);
        });
        // R downloads every package at once and says nothing until all have arrived: while it does, the size of what
        // has arrived is what shows the install is moving
        let shownBytes = 0;
        const isDownloading = () => status?.phase === 'downloading' || status?.phase === 'retrying';
        const watch = setInterval(async () => {
            if (!isDownloading()) {
                return;
            }
            const bytes = await folderSize(downloads);
            if (bytes !== shownBytes && isDownloading()) {
                shownBytes = bytes;
                options.onProgress?.({ ...status!, bytes });
            }
        }, 1000);
        const child = spawn(rscript, args, { env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
        for (const stream of [child.stdout, child.stderr]) {
            createInterface({ input: stream }).on('line', line => {
                if (line.startsWith('JOVIAN_PKG_ERROR: ')) {
                    failure = line.slice('JOVIAN_PKG_ERROR: '.length);
                } else if (line.startsWith('JOVIAN_PKG_OFFLINE: ')) {
                    offline = line.slice('JOVIAN_PKG_OFFLINE: '.length);
                } else if (line.startsWith('JOVIAN_PKG_RESULT: ')) {
                    const [before, after, installed, reached] = line.slice('JOVIAN_PKG_RESULT: '.length).split(' ');
                    result = {
                        previousVersion: before === 'NA' ? undefined : before,
                        version: after === 'NA' ? undefined : after,
                        installed: installed && installed !== '-' ? installed.split(',') : [],
                        offline: reached === 'offline'
                    };
                } else if (line.startsWith('JOVIAN_PKG: ')) {
                    options.onOutput?.(line.slice('JOVIAN_PKG: '.length));
                    follow(line.slice('JOVIAN_PKG: '.length), true);
                } else if (line.trim()) {
                    follow(line.trim(), false);
                    otherOutput.push(line.trim());
                    if (otherOutput.length > 400) {
                        otherOutput.splice(0, 200);
                    }
                }
            });
        }

        let timedOut = false;
        const timer = setTimeout(() => { timedOut = true; child.kill(); }, timeoutMs);
        const code = await new Promise<number | null>((resolve, reject) => {
            child.once('error', error => { clearTimeout(timer); reject(error); });
            child.once('close', exitCode => { clearTimeout(timer); resolve(exitCode); });
        }).finally(() => clearInterval(watch));

        if (timedOut) {
            throw new Error(`Installing ${request.name} took longer than ${Math.round(timeoutMs / 60000)} minutes and was stopped.`);
        }
        if (offline) {
            const error = new Error(offline);
            error.name = R_PACKAGES_OFFLINE;
            throw error;
        }
        if (code !== 0 || !result) {
            // R's error and what followed it, rather than the end of a compiler's output
            const errorAt = otherOutput.findIndex(line => /^Error\b/.test(line));
            const why = (errorAt >= 0 ? otherOutput.slice(errorAt) : otherOutput).slice(-5);
            throw new Error(failure ?? `Installing ${request.name} failed (Rscript exited with code ${code})${why.length ? `: ${why.join(' | ')}` : ''}`);
        }
        return result;
    } finally {
        if (directory) {
            await fs.promises.rm(directory, { recursive: true, force: true });
        }
        await unlock?.();
    }
}

/** The total size of the files in a folder (not its subfolders), in bytes; what can't be read counts as nothing. */
export async function folderSize(folder: string): Promise<number> {
    const entries = await fs.promises.readdir(folder, { withFileTypes: true }).catch(() => []);
    const sizes = await Promise.all(entries.filter(entry => entry.isFile())
        .map(entry => fs.promises.stat(join(folder, entry.name)).then(stat => stat.size, () => 0)));
    return sizes.reduce((sum, size) => sum + size, 0);
}
