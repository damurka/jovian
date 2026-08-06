import type { LoggerFunction } from '../types/index.js';

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

export class Logger {
    private customLogger?: LoggerFunction;

    constructor(customLogger?: LoggerFunction) {
        this.customLogger = customLogger;
    }

    trace(message: string, data?: any): void {
        if (this.customLogger) {
            this.customLogger('trace', message, data);
        } else {
            console.log(`${timestamp()} [trace] ${message}`, data ? data : '');
        }
    }

    debug(message: string, data?: any): void {
        if (this.customLogger) {
            this.customLogger('debug', message, data);
        } else {
            console.log(`${timestamp()} [debug] ${message}`, data ? data : '');
        }
    }

    info(message: string, data?: any): void {
        if (this.customLogger) {
            this.customLogger('info', message, data);
        } else {
            console.log(`${timestamp()} [info] ${message}`, data ? data : '');
        }
    }

    warn(message: string, data?: any): void {
        if (this.customLogger) {
            this.customLogger('warn', message, data);
        } else {
            console.warn(`${timestamp()} [warn] ${message}`, data ? data : '');
        }
    }

    error(message: string, error?: any): void {
        if (this.customLogger) {
            this.customLogger('error', message, error);
        } else {
            console.error(`${timestamp()} [error] ${message}`, error ? error : '');
        }
    }
}
