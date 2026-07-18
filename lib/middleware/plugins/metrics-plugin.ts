import type { Middleware } from '../middleware.js';

export class MetricsMiddleware implements Middleware {
    name = 'metrics';
    private messageCount = 0;
    private startTime = Date.now();

    process(message: string): string {
        this.messageCount++;
        const elapsed = (Date.now() - this.startTime) / 1000;
        const rate = this.messageCount / elapsed;
        
        console.log(`Messages/sec: ${rate.toFixed(2)}`);
        return message;
    }
}
