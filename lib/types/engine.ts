import type { JupyterMessage } from './messages.js';

export type LogLevel = 'trace' | 'debug' | 'info' | 'warn' | 'error';

export type LoggerFunction = (level: LogLevel, message: string, data?: any) => void;

export interface EngineOptions {
    /**
     * Which kernel a session runs: 'r' (Elara, the default -- every caller
     * that predates this field keeps behaving exactly as before) or
     * 'python' (Carpo). Selects which set of the fields below the
     * supervisor actually uses (native/src/themisto/session_registry.cpp's
     * SessionOptions::kernelType) and which kernel executable it spawns.
     */
    kernelType?: 'r' | 'python' | undefined;

    /** R installation (`R RHOME`). Found from $R_HOME, `R RHOME` or the Windows registry when omitted. */
    rHome?: string | undefined;
    rPath?: string | undefined;
    rLibs?: string | undefined;
    /** Directory containing the pandoc binary, for bundled R installs that don't ship it on PATH. */
    pandocPath?: string | undefined;
    /**
     * Source directory of the 'hera' R package (packages/hera in this repo).
     * When set, Elara installs it via remotes::install_local() if it is
     * missing or older than these sources (needs the 'remotes' package).
     * There is no default: without it, R sessions use whichever 'hera' is
     * already installed in the library -- install or update it with
     * `npm run hera:install`.
     */
    heraSrcPath?: string | undefined;

    /** Only used when kernelType is 'python' -- Carpo's equivalent of rHome. Found from $PYTHONHOME or `python3`/`python` on PATH when omitted. */
    pythonHome?: string | undefined;
    /** Only used when kernelType is 'python' -- Carpo's equivalent of rPath. */
    pythonPath?: string | undefined;
    /** Only used when kernelType is 'python' -- not yet consulted by Carpo itself (see carpo::EnvironmentConfig). */
    venvPath?: string | undefined;

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
    kernelType: 'r' | 'python';
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
