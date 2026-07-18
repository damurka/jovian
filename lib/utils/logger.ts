import type { LoggerFunction } from '../types/index.js';

export class Logger {
    private customLogger?: LoggerFunction;

    constructor(customLogger?: LoggerFunction) {
        this.customLogger = customLogger;
    }

    info(message: string, data?: any): void {
        if (this.customLogger) {
            this.customLogger('info', message, data);
        } else {
            console.log(`[INFO] ${message}`, data ? data : '');
        }
    }

    warn(message: string, data?: any): void {
        if (this.customLogger) {
            this.customLogger('warn', message, data);
        } else {
            console.warn(`[WARN] ${message}`, data ? data : '');
        }
    }

    error(message: string, error?: any): void {
        if (this.customLogger) {
            this.customLogger('error', message, error);
        } else {
            console.error(`[ERROR] ${message}`, error ? error : '');
        }
    }
}
