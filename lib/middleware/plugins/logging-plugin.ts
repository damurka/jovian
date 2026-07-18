import type { Middleware } from '../middleware.js';

export class LoggingMiddleware implements Middleware {
    name = 'logging';

    process(message: string): string {
        const timestamp = new Date().toISOString();
        console.log(`[${timestamp}] Message: ${message.substring(0, 100)}...`);
        return message;
    }
}
