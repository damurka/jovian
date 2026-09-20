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
    kernelType?: 'r' | 'python';

    rHome?: string;
    rPath?: string;
    rLibs?: string;
    /** Directory containing the pandoc binary, for bundled R installs that don't ship it on PATH. */
    pandocPath?: string;
    /**
     * Source directory of the 'hera' R package, used to auto-install it via
     * remotes::install_local() into rLibs if it isn't already installed.
     * Defaults to the copy bundled with this npm package.
     */
    heraSrcPath?: string;

    /** Only used when kernelType is 'python' -- Carpo's equivalent of rHome. */
    pythonHome?: string;
    /** Only used when kernelType is 'python' -- Carpo's equivalent of rPath. */
    pythonPath?: string;
    /** Only used when kernelType is 'python' -- not yet consulted by Carpo itself (see carpo::EnvironmentConfig). */
    venvPath?: string;

    queueSize?: number;
    enableLogging?: boolean;
    enableMetrics?: boolean;
    logger?: LoggerFunction;
}

export type EngineState = 
    | 'idle'
    | 'starting'
    | 'running'
    | 'stopping'
    | 'stopped'
    | 'error';

export interface ExecutionOptions {
    silent?: boolean;
    storeHistory?: boolean;
    allowStdin?: boolean;
    stopOnError?: boolean;
    timeout?: number;
}

export interface ExecutionResult {
    success: boolean;
    output: JupyterMessage[];
    error?: Error;
    executionCount?: number;
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
    histAccessType?: 'tail' | 'range' | 'search';
    /** Include each entry's output alongside its input. Defaults to false -- the kernel doesn't actually record output today either way, so this currently only ever comes back empty. */
    output?: boolean;
    raw?: boolean;
    /** Max entries to return ('tail'/'search'). Defaults to 100. */
    n?: number;
    /** 'range' only. */
    session?: number;
    start?: number;
    stop?: number;
    /** 'search' only -- a glob pattern (*, ?). */
    pattern?: string;
    unique?: boolean;
}

/**
 * One entry from a real history_reply: [session, line_number, input], or
 * [session, line_number, [input, output]] when `output: true` was requested
 * (see KernelHistoryOptions.output's own caveat -- output is currently
 * always "").
 */
export type KernelHistoryEntry = [number, number, string | [string, string]];

export interface ShinyAppOptions {
    /** Directory containing the Shiny app (server.R/ui.R or app.R). */
    appDir: string;
    /** Defaults to an OS-assigned free port. */
    port?: number;
    /** Defaults to '127.0.0.1'. */
    host?: string;
    /** Defaults to false -- the caller decides how/where to display the app. */
    launchBrowser?: boolean;
    /** Max time to wait for the app to start accepting connections, in ms. Defaults to 10000. */
    readyTimeout?: number;
    /**
     * Environment variables to set (via Sys.setenv()) in the R session
     * before launching the app -- e.g. rmncah's app.R reads
     * CDSUITE_SHINY_NAME/CDSUITE_SHINY_VERSION/CDSUITE_SHINY_SELECTED_FILE/
     * CDSUITE_SHINY_LOCALE via Sys.getenv(). Applied only for the duration
     * of this R session (not the OS process), and only take effect for code
     * that reads them after runApp() starts, since Sys.setenv() itself runs
     * synchronously right before it in the same execute() call.
     */
    env?: Record<string, string>;
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
