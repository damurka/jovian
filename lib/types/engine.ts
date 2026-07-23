import type { JupyterMessage } from './messages.js';

export type LoggerFunction = (level: 'info' | 'warn' | 'error', message: string, data?: any) => void;

export interface EngineOptions {
    rHome?: string;
    rPath?: string;
    rLibs?: string;
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
