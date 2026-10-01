import type { JupyterMessage } from './messages.js';

/**
 * 'notice' is for the few things a user should be told even when the library
 * is otherwise quiet (e.g. the one-time install of the R packages, which takes
 * a while); it sits between 'info' and 'warn'.
 */
export type LogLevel = 'trace' | 'debug' | 'info' | 'notice' | 'warn' | 'error';

/** What the built-in console logger prints: this level and above; 'silent' prints nothing. */
export type LogThreshold = LogLevel | 'silent';

export interface SessionManagerOptions {
    /**
     * How much the library prints to the console (default 'notice': one-time
     * setup messages, warnings and errors; env JOVIAN_LOG_LEVEL sets the
     * default). Ignored for messages sent to `logger`, which receives all of them.
     */
    logLevel?: LogThreshold | undefined;
    /** Receives every log message instead of the console. */
    logger?: LoggerFunction | undefined;
    /**
     * Print the kernels' own start-up output (the `[elara]` / `[carpo]` lines)
     * to stderr as it happens. Off by default -- it is included in the error
     * when a kernel fails to start -- and on when logLevel is 'debug' or
     * 'trace' or env JOVIAN_KERNEL_OUTPUT is set.
     */
    kernelOutput?: boolean | undefined;
    /**
     * Answer completion and inspection for a busy R session from a second,
     * idle R process with the same packages attached (default true). R cannot
     * answer them itself while a cell runs. The helper is started when an R
     * cell has been running for a second, one per R installation, shared by
     * the sessions using it, and stopped with them; it costs one more R
     * process. Set false to have such requests wait for the cell instead.
     */
    busyHelper?: boolean | undefined;
    /**
     * Keep the supervisor (and so every session) running when this process
     * exits, so the next SessionManager -- after a window reload, a restart
     * of the host app -- reconnects to it: `true`, or where to record it and
     * how long it may sit unused. See SessionManager.listSessions() and
     * attachSession(). Default: off -- sessions end with this process.
     */
    persistent?: boolean | PersistentOptions | undefined;
}

export interface PersistentOptions {
    /**
     * Where the running supervisor is recorded (address and access token --
     * written readable by this user only). SessionManagers with the same
     * stateFile share one supervisor. Default: ~/.jovian/supervisor.json.
     */
    stateFile?: string | undefined;
    /**
     * The supervisor stops itself, and every session in it, after this many
     * minutes with no client connected (default 60).
     */
    idleShutdownMinutes?: number | undefined;
}

/** How Session.complete() / inspect() behave while a cell is running. */
export interface BusyRequestOptions {
    /**
     * When the kernel (or, for R, its helper -- see
     * SessionManagerOptions.busyHelper) cannot answer until the running cell
     * finishes: wait for it (true, the default), or resolve at once with
     * nothing found and `metadata['jovian/busy']` set (false) -- for
     * as-you-type requests that should not sit behind a long cell.
     */
    waitForCell?: boolean | undefined;
    /** How long to wait for the reply, in ms (default 10000) -- raise it to wait out a long cell. */
    timeout?: number | undefined;
}

export type LoggerFunction = (level: LogLevel, message: string, data?: any) => void;

/** The kernels a session can run: R (Elara), Python (Carpo) or Stata (Callisto). */
export type KernelType = 'r' | 'python' | 'stata' | 'ark';

export interface EngineOptions {
    /**
     * Which kernel a session runs: 'r' (Elara, the default -- every caller
     * that predates this field keeps behaving exactly as before), 'python'
     * (Carpo) or 'stata' (Callisto). Selects which set of the fields below the
     * supervisor actually uses (native/src/themisto/session_registry.cpp's
     * SessionOptions::kernelType) and which kernel executable it spawns.
     */
    kernelType?: KernelType | undefined;

    /** R installation (`R RHOME`). Found from $R_HOME, `R RHOME` or the Windows registry when omitted. */
    rHome?: string | undefined;
    rPath?: string | undefined;
    rLibs?: string | undefined;
    /** Directory containing the pandoc binary, for bundled R installs that don't ship it on PATH. */
    pandocPath?: string | undefined;
    /**
     * For developing hera, the R kernel's own R code (packages/hera in this
     * repo): Elara reads it from this directory instead of the copy built
     * into it, so an edit takes effect at the next session without
     * rebuilding the kernel. Nothing is installed either way.
     */
    heraSrcPath?: string | undefined;

    /** Only used when kernelType is 'python' -- Carpo's equivalent of rHome. Found from $PYTHONHOME or `python3`/`python` on PATH when omitted. */
    pythonHome?: string | undefined;
    /** Only used when kernelType is 'python' -- Carpo's equivalent of rPath. */
    pythonPath?: string | undefined;
    /** Only used when kernelType is 'python' -- not yet consulted by Carpo itself (see carpo::EnvironmentConfig). */
    venvPath?: string | undefined;

    /**
     * Only used when kernelType is 'stata' -- the directory Stata (17 or
     * newer) is installed in, the one holding its executable and shared
     * library (e.g. `C:\Program Files\StataNow19`, `/usr/local/stata19`,
     * `/Applications/StataNow`). Found from $STATA_HOME or the usual install
     * locations when omitted.
     */
    stataHome?: string | undefined;
    /**
     * Only used when kernelType is 'stata' -- which edition to load when more
     * than one is installed in stataHome. Default: the first of 'mp', 'se',
     * 'be' that is there.
     */
    stataEdition?: 'mp' | 'se' | 'be' | undefined;

    /**
     * Only used when kernelType is 'ark' -- Posit's Ark R kernel (the one in
     * Positron), run by the same supervisor instead of Elara: an experiment,
     * see docs/kernels.md. The ark executable; found from $ARK_PATH, Positron's
     * install, or ark on PATH when omitted. R comes from rHome, as for 'r'.
     */
    arkPath?: string | undefined;

    /**
     * Directory the kernel process starts in -- what `getwd()` (R) /
     * `os.getcwd()` (Python) report and what relative paths resolve against.
     * Must already exist. Defaults to the supervisor's own working directory
     * (i.e. the calling process's), which is rarely what you want for a
     * notebook/project: set it to the project or document folder.
     */
    workingDirectory?: string | undefined;

    queueSize?: number | undefined;
    enableLogging?: boolean | undefined;
    enableMetrics?: boolean | undefined;
    logger?: LoggerFunction | undefined;
}

export type EngineState = 
    | 'idle'
    | 'starting'
    | 'running'
    | 'stopping'
    | 'stopped'
    | 'error';

export interface ExecutionOptions {
    silent?: boolean | undefined;
    storeHistory?: boolean | undefined;
    allowStdin?: boolean | undefined;
    /**
     * When this execution fails, abort every execute() still waiting behind
     * it in the queue instead of running them (Jupyter's stop_on_error) --
     * their results come back with `aborted: true` and nothing having run.
     * Also forwarded to the kernel in the execute_request itself.
     */
    stopOnError?: boolean | undefined;
    /**
     * Expressions to evaluate in the kernel right after the code runs, as
     * {name: expression} (Jupyter's user_expressions). Only evaluated when
     * the code succeeded; each result -- or its own error -- comes back in
     * `ExecutionResult.userExpressions` under the same name.
     */
    userExpressions?: Record<string, string> | undefined;
    /**
     * Milliseconds to wait for the execution to finish before giving up
     * (default 30000; 0 = no timeout, for calls meant to run indefinitely
     * such as a Shiny app).
     */
    timeout?: number | undefined;
    /**
     * When the timeout fires, also send the kernel an interrupt (default
     * true) so it stops the code instead of carrying on with work no one is
     * waiting for -- which would otherwise block everything queued behind it.
     * Set false to leave the kernel running after a timeout.
     */
    interruptOnTimeout?: boolean | undefined;
}

/** One evaluated user expression: its rich value, or the error evaluating it raised. */
export type UserExpressionResult =
    | { status: 'ok'; data: Record<string, any>; metadata: Record<string, any> }
    | { status: 'error'; ename: string; evalue: string; traceback: string[] };

export interface ExecutionResult {
    success: boolean;
    output: JupyterMessage[];
    error?: Error;
    executionCount?: number;
    /** The execute_reply's own status; 'aborted' means it never ran (see ExecutionOptions.stopOnError). */
    status?: 'ok' | 'error' | 'aborted';
    /** True when this execution was skipped because an earlier one failed with stopOnError. */
    aborted?: boolean;
    /** Results of ExecutionOptions.userExpressions, by name. */
    userExpressions?: Record<string, UserExpressionResult>;
}

/**
 * One past execute() call's full record: the code that ran, its kernel-
 * reported execution count (if the reply included one -- a silent execution
 * never gets one), and every iopub message it produced (stream/
 * execute_result/display_data/error/status/...), in arrival order. Kept by
 * Session.getHistory() for the life of that Session object -- purely
 * in-memory, gone once the Session (or its owning process) does, same as
 * everything else client-side. See Session.getHistory()'s own doc comment
 * for why this exists and how it differs from queryKernelHistory().
 */
export interface ExecutionHistoryEntry {
    code: string;
    executionCount?: number;
    time: number;
    messages: JupyterMessage[];
    /**
     * True when this execution printed so much that the oldest stream
     * (stdout/stderr) messages were dropped to bound memory: the entry keeps
     * the most recent ~500 KB of stream text and everything else (results,
     * display data, errors) in full.
     */
    truncated?: boolean;
}

/**
 * Options for Session.queryKernelHistory(), mirroring Jupyter's real
 * history_request wire message (KernelCore::historyRequest() ->
 * HistoryManager::processRequest(), native/src/adrastea/core/history/) --
 * see that function's own field reads for exactly which of these apply to
 * which histAccessType.
 */
export interface KernelHistoryOptions {
    /** Defaults to 'tail' -- the n most recent executions. */
    histAccessType?: 'tail' | 'range' | 'search' | undefined;
    /** Include each entry's output alongside its input. Defaults to false -- the kernel doesn't actually record output today either way, so this currently only ever comes back empty. */
    output?: boolean | undefined;
    raw?: boolean | undefined;
    /** Max entries to return ('tail'/'search'). Defaults to 100. */
    n?: number | undefined;
    /** 'range' only. */
    session?: number | undefined;
    start?: number | undefined;
    stop?: number | undefined;
    /** 'search' only -- a glob pattern (*, ?). */
    pattern?: string | undefined;
    unique?: boolean | undefined;
}

/**
 * One entry from a real history_reply: [session, line_number, input], or
 * [session, line_number, [input, output]] when `output: true` was requested
 * (see KernelHistoryOptions.output's own caveat -- output is currently
 * always "").
 */
export type KernelHistoryEntry = [number, number, string | [string, string]];

/** The supervisor's latest view of a kernel's heartbeat -- see Session.status(). */
export interface HeartbeatInfo {
    /** False until the first ping has been answered. */
    hasPong: boolean;
    /** Round trip of the most recent answered ping, in ms. */
    rttMs: number;
    /** How long ago that answer arrived, in ms. */
    sinceLastPongMs: number;
    /** Pings in a row that went unanswered (0 = healthy). */
    misses: number;
}

/** What the supervisor knows about one session's kernel process. */
export interface SessionStatusInfo {
    sessionId: string;
    status: 'starting' | 'ready' | 'stopped' | 'crashed';
    kernelType: KernelType;
    workingDirectory: string;
    /** OS process id of the kernel; 0 when it is not running. */
    pid: number;
    /** Resident memory in bytes, or null where it can't be read. */
    memoryBytes: number | null;
    heartbeat: HeartbeatInfo | null;
}

export interface ShinyAppOptions {
    /** Directory containing the Shiny app (server.R/ui.R or app.R). */
    appDir: string;
    /** Defaults to an OS-assigned free port. */
    port?: number | undefined;
    /** Defaults to '127.0.0.1'. */
    host?: string | undefined;
    /** Defaults to false -- the caller decides how/where to display the app. */
    launchBrowser?: boolean | undefined;
    /** Max time to wait for the app to start accepting connections, in ms. Defaults to 10000. */
    readyTimeout?: number | undefined;
    /**
     * Environment variables to set (via Sys.setenv()) in the R session
     * before launching the app -- e.g. rmncah's app.R reads
     * CDSUITE_SHINY_NAME/CDSUITE_SHINY_VERSION/CDSUITE_SHINY_SELECTED_FILE/
     * CDSUITE_SHINY_LOCALE via Sys.getenv(). Applied only for the duration
     * of this R session (not the OS process), and only take effect for code
     * that reads them after runApp() starts, since Sys.setenv() itself runs
     * synchronously right before it in the same execute() call.
     */
    env?: Record<string, string> | undefined;
}

export interface ShinyAppHandle {
    host: string;
    port: number;
    url: string;
    /**
     * Resolves with the R-side execute_reply once the app stops (e.g. via
     * interrupt/restart) or crashes -- shiny::runApp() blocks the R session
     * for as long as the app is running, so this does NOT resolve just
     * because the app started successfully. Await `createShiny()` itself
     * for that.
     */
    done: Promise<ExecutionResult>;
}

/**
 * Reply contents for the Jupyter request/reply pairs Session exposes as
 * methods -- see Session.complete()/inspect()/isComplete()/kernelInfo()/
 * commInfo()/interrupt(). All of them carry the standard `status`.
 */
export interface CompleteReplyContent {
    status: 'ok' | 'error';
    matches: string[];
    cursor_start: number;
    cursor_end: number;
    metadata: Record<string, any>;
}

export interface InspectReplyContent {
    status: 'ok' | 'error';
    found: boolean;
    data: Record<string, any>;
    metadata: Record<string, any>;
}

export interface IsCompleteReplyContent {
    status: 'complete' | 'incomplete' | 'invalid' | 'unknown';
    indent?: string;
}

export interface KernelInfoReplyContent {
    status: 'ok' | 'error';
    protocol_version: string;
    implementation: string;
    implementation_version: string;
    language_info: {
        name: string;
        version: string;
        mimetype: string;
        file_extension: string;
        pygments_lexer?: string;
        codemirror_mode?: string | Record<string, any>;
        nbconvert_exporter?: string;
    };
    banner: string;
    help_links?: Array<{ text: string; url: string }>;
}

export interface CommInfoReplyContent {
    status: 'ok' | 'error';
    /** comm_id -> {target_name} for every comm currently open in the kernel. */
    comms: Record<string, { target_name: string }>;
}

export interface InterruptReplyContent {
    status: 'ok' | 'error';
}

export interface ShutdownReplyContent {
    status: 'ok' | 'error';
    restart: boolean;
}

/** An object of an R or Python session (Session.listVariables()). */
export interface SessionVariable {
    name: string;
    /** R: its class (`data.frame`, `numeric`, `function`); Python: its type's name (`DataFrame`, `int`). */
    type: string;
    /** Rows × columns of a table or an array, the length of a vector or a container; empty when it has none. */
    size: string;
    /** What it holds, in one line (at most about 200 characters). */
    summary: string;
    /** Session.readTable() reads it: an R data frame or matrix, a pandas DataFrame or Series, a numpy array, a polars DataFrame. */
    table: boolean;
}

/** Rows of a table (Session.readTable()): `rows[i][j]` is column `columns[j]` of row `start + i`, as text. */
export interface TablePage {
    name: string;
    /** All the rows the table has. */
    rowCount: number;
    /** Its columns: name, and type (R: class; Python: dtype). */
    columns: Array<{ name: string; type: string }>;
    start: number;
    /** How many rows came back (fewer than asked at the end). */
    count: number;
    /** The rows' names when they are more than their numbers (R row names, a pandas index), else null. */
    rowLabels: string[] | null;
    rows: string[][];
}

/** A variable of a Stata dataset (Session.stataDataset()). */
export interface StataVariable {
    name: string;
    /** Storage type: `byte`, `int`, `long`, `float`, `double`, `str18`, `strL`. */
    type: string;
    /** Display format: `%9.0g`, `%td`, `%-18s` ... */
    format: string;
    /** Variable label (empty when none). */
    label: string;
    /** Name of its value label (a key of StataDataset.valueLabels when it is defined), or null. */
    valueLabel: string | null;
}

/** The dataset in a Stata session's memory (Session.stataDataset()). */
export interface StataDataset {
    /** The current frame (`default` unless the user changed frames). */
    frame: string;
    observations: number;
    /** The file it was loaded from (c(filename)), empty when none. */
    filename: string;
    /** Whether it changed since it was loaded or saved (c(changed)). */
    changed: boolean;
    variables: StataVariable[];
    /** Value labels by name: their values and texts (at most 1000 of each). */
    valueLabels: Record<string, { values: number[]; labels: string[] }>;
}

/** What Session.stataData() reads. */
export interface StataDataOptions {
    /** First observation, from 1 (default 1). */
    start?: number | undefined;
    /** How many observations (default 100, at most 100 000). */
    count?: number | undefined;
    /** Which variables, in this order (default all). */
    variables?: string[] | undefined;
    /** Values as Stata's Data Editor shows them: strings with value labels and display formats applied. */
    formatted?: boolean | undefined;
    timeout?: number | undefined;
}

/**
 * Observations of a Stata dataset (Session.stataData()): `rows[i][j]` is variable `variables[j]` of observation
 * `start + i`. Raw values are numbers, strings (string variables), `null` (the missing value `.`) or, in a numeric
 * variable, `".a"` to `".z"` (extended missing values); formatted ones are all strings.
 */
export interface StataDataPage {
    start: number;
    /** How many rows came back (fewer than asked at the end of the data). */
    count: number;
    /** The dataset's number of observations. */
    observations: number;
    variables: string[];
    formatted: boolean;
    rows: Array<Array<number | string | null>>;
}

/**
 * A request of the R code to the host's UI: Session's 'ui' event. What R's rstudioapi asks of RStudio (the R kernel's
 * .rs.api.* functions): open a file (`navigateToFile`), show a URL (`viewer`), ask a question (`showPrompt`,
 * `showQuestion`, `askForPassword`), read the editor (`getActiveDocumentContext`) ... A notification has no
 * `reply`; a question has one, and the R code waits for it: call it with the answer (null for none, which gives the
 * R function its default). A question nobody listens for is answered with none.
 */
export interface UiRequest {
    method: string;
    params: Record<string, unknown>;
    reply?: ((answer: unknown) => void) | undefined;
}

/** A Debug Adapter Protocol response: Session.debugRequest(). */
export interface DapResponse<T = any> {
    type: 'response';
    seq: number;
    request_seq: number;
    success: boolean;
    command: string;
    message?: string | undefined;
    body?: T | undefined;
}

/** An installed R package: Session.listPackages(). */
export interface RPackageInfo {
    name: string;
    version: string;
    /** The library it is installed in (the one R loads it from, when in several). */
    library: string;
    /** 'base' or 'recommended' for the packages that come with R, else ''. */
    priority: string;
    loaded: boolean;
    attached: boolean;
}

/** Whether an R package is installed (at least at the version asked for): Session.packagesInstalled(). */
export interface RPackageCheck {
    name: string;
    /** The installed version, or null. */
    version: string | null;
    installed: boolean;
}

/** An installed R package with a newer version available: Session.outdatedPackages(). */
export interface RPackageUpdate {
    name: string;
    installed: string;
    available: string;
    library: string;
    repository: string;
}

/** An R package in the repositories: Session.searchPackages(). */
export interface RPackageSearchResult {
    name: string;
    version: string;
    repository: string;
}

export interface RPackageOptions {
    /** Repositories to look in before the session's own (an r-universe, say); CRAN's cloud mirror when it has none. */
    repos?: string[] | undefined;
    /** The library to install into (or remove from); the session's first by default. */
    lib?: string | undefined;
    /** How long to wait, in ms; installs default to 30 minutes. */
    timeout?: number | undefined;
}

/** What Session.installPackages() did. */
export interface RPackageInstallResult {
    /** The version now installed of each package asked for (null: not installed). */
    installed: Array<{ name: string; version: string | null }>;
    /** The packages that could not be installed. */
    failed: string[];
    /** R's warnings while installing (why a package failed, usually). */
    warnings: string[];
}
