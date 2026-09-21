import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import type { EngineOptions } from '../types/index.js';

/**
 * Runs once, before the first R session, when the library was given a copy of
 * the 'hera' R package (always the case for an npm install): makes sure hera
 * and everything it needs are installed, so a person who has just installed R
 * has nothing to do by hand.
 *
 * It is a separate step, not part of the kernel's start-up, because the
 * supervisor gives a kernel 60 seconds to register and installing a dozen
 * packages (compiling some, on Linux) takes longer. A kernel started after
 * this finds hera already there.
 *
 * Only what is missing is installed, into the first writable library, and
 * nothing is installed at all when hera is already current. A package that is
 * installed but cannot be loaded (Debian/Ubuntu `r-cran-*` packages built for
 * an older R fail with "undefined symbol: SETLENGTH") counts as missing and is
 * reinstalled from CRAN into the user's own library, which R searches first.
 */
export const R_SETUP_SCRIPT = String.raw`
src <- commandArgs(trailingOnly = TRUE)[1]
say <- function(...) cat("JOVIAN_R_SETUP: ", ..., "\n", sep = "")
note <- function(...) cat("JOVIAN_R_SETUP_INFO: ", ..., "\n", sep = "")
fail <- function(...) {
    cat("JOVIAN_R_SETUP_ERROR: ", ..., "\n", sep = "")
    quit(save = "no", status = 1)
}
field <- function(path, name) {
    value <- tryCatch(read.dcf(path, fields = name)[1, 1], error = function(e) NA_character_)
    if (is.na(value)) NA_character_ else value
}
stamp_wanted <- field(file.path(src, "DESCRIPTION"), "Config/jovian/release")

hera_current <- function() {
    installed <- tryCatch(find.package("hera", quiet = TRUE), error = function(e) character(0))
    if (!length(installed)) return(FALSE)
    if (!is.na(stamp_wanted) && !identical(field(file.path(installed, "DESCRIPTION"), "Config/jovian/release"), stamp_wanted)) return(FALSE)
    suppressWarnings(suppressMessages(requireNamespace("hera", quietly = TRUE)))
}
if (hera_current()) {
    note("hera is already installed")
    quit(save = "no", status = 0)
}

repos <- getOption("repos")
if (is.null(repos) || is.na(repos["CRAN"]) || identical(unname(repos["CRAN"]), "@CRAN@")) {
    options(repos = c(CRAN = "https://cloud.r-project.org"))
}

is_writable <- function(path) dir.exists(path) && file.access(path, 2) == 0
lib <- Filter(is_writable, .libPaths())[1]
if (is.na(lib)) {
    lib <- Sys.getenv("R_LIBS_USER")
    if (!nzchar(lib)) lib <- file.path(Sys.getenv("HOME"), "R", "library")
    dir.create(lib, recursive = TRUE, showWarnings = FALSE)
    .libPaths(c(lib, .libPaths()))
}

for (lock in Sys.glob(file.path(lib, "00LOCK-*"))) {
    # An install that was killed (Ctrl+C, a timeout) leaves its lock behind, and
    # every later install of that package is refused until it is removed.
    if (difftime(Sys.time(), file.info(lock)$mtime, units = "mins") > 5) {
        say("removing a stale lock left by an interrupted install: ", lock)
        unlink(lock, recursive = TRUE)
    }
}

ip <- installed.packages()
base_packages <- rownames(ip)[!is.na(ip[, "Priority"])]
fields <- tryCatch(read.dcf(file.path(src, "DESCRIPTION"), fields = c("Depends", "Imports", "LinkingTo")), error = function(e) NULL)
direct <- unique(trimws(sub("[(].*$", "", unlist(strsplit(paste(stats::na.omit(as.vector(fields)), collapse = ","), ",")))))
direct <- setdiff(direct[nzchar(direct)], c("R", base_packages))

available <- tryCatch(suppressWarnings(available.packages()), error = function(e) NULL)
wanted <- direct
if (!is.null(available) && nrow(available) > 0) {
    everything <- tools::package_dependencies(direct, db = available, recursive = TRUE)
    wanted <- setdiff(unique(c(direct, unlist(everything, use.names = FALSE))), base_packages)
}

usable <- function(package) suppressWarnings(suppressMessages(requireNamespace(package, quietly = TRUE)))
missing <- wanted[!vapply(wanted, usable, logical(1))]
warnings_seen <- character()
collect <- function(expr) withCallingHandlers(expr, warning = function(w) {
    warnings_seen <<- c(warnings_seen, conditionMessage(w))
    invokeRestart("muffleWarning")
})

if (length(missing) > 0) {
    if (is.null(available) || nrow(available) == 0) {
        fail("these R packages are needed but not installed: ", paste(missing, collapse = ", "),
             " -- and CRAN could not be reached to install them (check the internet connection)")
    }
    say("installing ", length(missing), " R package(s) from CRAN into ", lib, " (first run only): ", paste(missing, collapse = ", "))
    tryCatch(collect(utils::install.packages(missing, lib = lib, repos = getOption("repos"), quiet = TRUE)),
             error = function(e) fail("installing R packages failed: ", conditionMessage(e)))
    still_missing <- missing[!vapply(missing, usable, logical(1))]
    if (length(still_missing) > 0) {
        # The warnings only say "non-zero exit status". Packages that need
        # another one that failed fail too, so re-run the install of the ones
        # that do not, on their own, and report what R and the compiler said.
        needs <- tryCatch(tools::package_dependencies(still_missing, db = available, recursive = FALSE), error = function(e) list())
        roots <- still_missing[vapply(still_missing, function(p) !any(needs[[p]] %in% still_missing), logical(1))]
        reasons <- character()
        for (p in utils::head(roots, 2)) {
            why <- tryCatch({
                tarball <- utils::download.packages(p, destdir = tempdir(), repos = getOption("repos"), type = "source", quiet = TRUE)[1, 2]
                out <- suppressWarnings(system2(file.path(R.home("bin"), "R"), c("CMD", "INSTALL", paste0("--library=", shQuote(lib)), shQuote(tarball)), stdout = TRUE, stderr = TRUE))
                if (is.null(attr(out, "status")) || identical(attr(out, "status"), 0L)) {
                    "it installed when retried on its own, so the failure may be temporary: try again"
                } else {
                    important <- grep("error|ERROR|undefined|fatal|cannot|not found|No such file|Killed", out, value = TRUE)
                    substr(gsub("[[:space:]]+", " ", paste(utils::tail(if (length(important)) important else out, 5), collapse = " ; ")), 1, 700)
                }
            }, error = function(e) conditionMessage(e))
            reasons <- c(reasons, paste0(p, ": ", why))
        }
        fail("could not install these R packages: ", paste(still_missing, collapse = ", "),
             if (length(reasons)) paste0(" -- ", paste(reasons, collapse = " | ")) else "",
             " (on Linux packages are compiled from source and need a compiler: sudo apt install build-essential)")
    }
}

say("installing hera into ", lib)
tryCatch(collect(utils::install.packages(src, repos = NULL, type = "source", lib = lib, quiet = TRUE)),
         error = function(e) NULL)
if (!hera_current()) {
    output <- tryCatch(
        suppressWarnings(system2(file.path(R.home("bin"), "R"), c("CMD", "INSTALL", paste0("--library=", shQuote(lib)), shQuote(src)), stdout = TRUE, stderr = TRUE)),
        error = function(e) conditionMessage(e))
    lines <- grep("ERROR|error|not available|cannot|denied|failed", output, value = TRUE)
    reason <- paste(utils::tail(if (length(lines)) lines else output, 6), collapse = " | ")
    fail("hera could not be installed: ", substr(gsub("[[:space:]]+", " ", reason), 1, 800))
}
say("done")
`;

export interface SetupLogger {
    debug(message: string): void;
    info(message: string): void;
    notice(message: string): void;
}

export interface SetupProcessOutcome {
    code: number | null;
    timedOut: boolean;
}

export interface SetupDeps {
    env: NodeJS.ProcessEnv;
    platform: string;
    exists: (path: string) => boolean;
    /** Runs `rscript scriptFile ...args`, reporting each stdout/stderr line. */
    run: (rscript: string, scriptFile: string, args: string[], env: NodeJS.ProcessEnv, onLine: (line: string) => void, timeoutMs: number) => Promise<SetupProcessOutcome>;
    timeoutMs: number;
}

const runRscript: SetupDeps['run'] = async (rscript, scriptFile, args, env, onLine, timeoutMs) => {
    const child = spawn(rscript, [scriptFile, ...args], { env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
    for (const stream of [child.stdout, child.stderr]) {
        createInterface({ input: stream }).on('line', onLine);
    }
    let timedOut = false;
    const timer = setTimeout(() => {
        timedOut = true;
        child.kill();
    }, timeoutMs);
    return new Promise((resolve, reject) => {
        child.once('error', (error) => {
            clearTimeout(timer);
            reject(error);
        });
        child.once('close', (code) => {
            clearTimeout(timer);
            resolve({ code, timedOut });
        });
    });
};

const defaultDeps = (): SetupDeps => ({
    env: process.env,
    platform: process.platform,
    exists: existsSync,
    run: runRscript,
    timeoutMs: 30 * 60 * 1000
});

export function rscriptPath(rHome: string, deps: Pick<SetupDeps, 'platform' | 'exists'> = defaultDeps()): string | undefined {
    const name = deps.platform === 'win32' ? 'Rscript.exe' : 'Rscript';
    const candidates = [join(rHome, 'bin', name)];
    if (deps.platform === 'win32') candidates.push(join(rHome, 'bin', 'x64', name));
    return candidates.find((candidate) => deps.exists(candidate));
}

const inFlight = new Map<string, Promise<void>>();

/**
 * Makes sure hera and its dependencies are installed for this R (see the
 * comment on R_SETUP_SCRIPT). Resolves immediately when there is nothing to
 * do: a Python session, no bundled hera (a source checkout), no R found, or
 * JOVIAN_SKIP_R_SETUP set. Concurrent calls for the same R share one run, and
 * a run that succeeded is not repeated by this process. Rejects with R's own
 * explanation when the packages could not be installed.
 */
export function ensureRPackages(options: EngineOptions, logger: SetupLogger, deps: SetupDeps = defaultDeps()): Promise<void> {
    const { rHome, heraSrcPath } = options;
    if (options.kernelType === 'python' || !rHome || !heraSrcPath) return Promise.resolve();
    if (deps.env.JOVIAN_SKIP_R_SETUP) return Promise.resolve();

    const rscript = rscriptPath(rHome, deps);
    if (!rscript) {
        logger.debug(`No Rscript under ${rHome}; leaving the R packages to the kernel`);
        return Promise.resolve();
    }

    const key = [rHome, heraSrcPath, options.rLibs ?? ''].join('|');
    let pending = inFlight.get(key);
    if (!pending) {
        pending = setUp(rscript, heraSrcPath, options.rLibs, logger, deps).catch((error) => {
            inFlight.delete(key);
            throw error;
        });
        inFlight.set(key, pending);
    }
    return pending;
}

async function setUp(rscript: string, heraSrcPath: string, rLibs: string | undefined, logger: SetupLogger, deps: SetupDeps): Promise<void> {
    const directory = await mkdtemp(join(tmpdir(), 'jovian-r-setup-'));
    try {
        const scriptFile = join(directory, 'setup.R');
        await writeFile(scriptFile, R_SETUP_SCRIPT);

        const env = { ...deps.env, ...(rLibs ? { R_LIBS: rLibs } : {}) };
        let failure: string | undefined;
        const otherOutput: string[] = [];
        const onLine = (line: string) => {
            if (line.startsWith('JOVIAN_R_SETUP_ERROR: ')) failure = line.slice('JOVIAN_R_SETUP_ERROR: '.length);
            else if (line.startsWith('JOVIAN_R_SETUP: ')) logger.notice(`R setup: ${line.slice('JOVIAN_R_SETUP: '.length)}`);
            else if (line.startsWith('JOVIAN_R_SETUP_INFO: ')) logger.info(`R setup: ${line.slice('JOVIAN_R_SETUP_INFO: '.length)}`);
            else if (line.trim()) otherOutput.push(line.trim());
        };

        const outcome = await deps.run(rscript, scriptFile, [heraSrcPath], env, onLine, deps.timeoutMs);
        if (outcome.timedOut) {
            throw new Error(`Setting up the R packages the kernel needs took longer than ${Math.round(deps.timeoutMs / 60000)} minutes and was stopped.`);
        }
        if (outcome.code !== 0) {
            const unexplained = `Rscript exited with code ${outcome.code}${otherOutput.length ? `: ${otherOutput.slice(-5).join(' | ')}` : ''}`;
            throw new Error(`Could not set up the R packages the kernel needs: ${failure ?? unexplained}`);
        }
    } finally {
        await rm(directory, { recursive: true, force: true });
    }
}
