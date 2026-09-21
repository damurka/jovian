import type { LogLevel, LoggerFunction, LogThreshold } from '../types/index.js';

// Matches the timestamp format used throughout VS Code's own logs
// (exthost.log, output channels, etc.): "YYYY-MM-DD HH:mm:ss.mmm". Kept
// local rather than pulled from a date library so this has no runtime
// dependency beyond what's already here. Exported for the handful of call
// sites (e.g. session-worker.ts) that log before a Logger instance exists.
export function timestamp(): string {
    const now = new Date();
    const pad = (n: number, width = 2) => String(n).padStart(width, '0');
    return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())} ` +
        `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}.${pad(now.getMilliseconds(), 3)}`;
}

const ORDER: Record<LogThreshold, number> = { trace: 0, debug: 1, info: 2, notice: 3, warn: 4, error: 5, silent: 6 };

/** True when a message at `level` passes `threshold`. */
export function passes(threshold: LogThreshold, level: LogLevel): boolean {
    return ORDER[level] >= ORDER[threshold];
}

/** The console threshold when none is given: $JOVIAN_LOG_LEVEL if it names one, else 'notice'. */
export function defaultLogLevel(env: Record<string, string | undefined> = process.env): LogThreshold {
    const wanted = env.JOVIAN_LOG_LEVEL?.trim().toLowerCase();
    return wanted && wanted in ORDER ? (wanted as LogThreshold) : 'notice';
}

export class Logger {
    private readonly customLogger: LoggerFunction | undefined;
    private readonly threshold: LogThreshold;

    /**
     * A custom logger receives every message (it does its own filtering, as it
     * always has); `threshold` only limits what is printed to the console.
     */
    constructor(customLogger?: LoggerFunction, threshold: LogThreshold = defaultLogLevel()) {
        this.customLogger = customLogger;
        this.threshold = threshold;
    }

    private emit(level: LogLevel, message: string, data: unknown): void {
        if (this.customLogger) {
            this.customLogger(level, message, data);
            return;
        }
        if (!passes(this.threshold, level)) return;
        const print = level === 'warn' ? console.warn : level === 'error' ? console.error : console.log;
        const line = `${timestamp()} [${level}] ${message}`;
        if (data) print(line, data);
        else print(line);
    }

    trace(message: string, data?: any): void {
        this.emit('trace', message, data);
    }

    debug(message: string, data?: any): void {
        this.emit('debug', message, data);
    }

    info(message: string, data?: any): void {
        this.emit('info', message, data);
    }

    notice(message: string, data?: any): void {
        this.emit('notice', message, data);
    }

    warn(message: string, data?: any): void {
        this.emit('warn', message, data);
    }

    error(message: string, error?: any): void {
        this.emit('error', message, error);
    }
}
