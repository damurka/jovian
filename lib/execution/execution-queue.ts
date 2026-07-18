import type { ExecutionOptions, ExecutionResult } from '../types/index.js';

interface QueuedExecution {
    code: string;
    options: ExecutionOptions;
    resolve: (result: ExecutionResult) => void;
    reject: (error: Error) => void;
    timestamp: number;
}

export class ExecutionQueue {
    private queue: QueuedExecution[] = [];
    private executing: boolean = false;
    private addon: any;
    private maxSize: number;

    constructor(addon: any, maxSize: number = 100) {
        this.addon = addon;
        this.maxSize = maxSize;
    }

    execute(code: string, options: ExecutionOptions = {}): Promise<ExecutionResult> {
        return new Promise((resolve, reject) => {
            if (this.queue.length >= this.maxSize) {
                reject(new Error('Execution queue is full'));
                return;
            }

            this.queue.push({
                code,
                options,
                resolve,
                reject,
                timestamp: Date.now()
            });

            // Start processing if not already executing
            if (!this.executing) {
                this.processNext();
            }
        });
    }

    private async processNext(): Promise<void> {
        if (this.queue.length === 0) {
            this.executing = false;
            return;
        }

        this.executing = true;
        const item = this.queue.shift()!;

        try {
            // Execute via addon
            this.addon.execute(item.code);
            
            // Wait for result (simplified - in reality, you'd listen to events)
            const result = await this.waitForResult(item);
            item.resolve(result);
        } catch (error) {
            item.reject(error as Error);
        }

        // Process next item
        this.processNext();
    }

    private waitForResult(item: QueuedExecution): Promise<ExecutionResult> {
        // This would integrate with the message router
        // to capture the result of this specific execution
        return new Promise((resolve) => {
            // Implementation depends on your message tracking
            // For now, we just resolve immediately as the addon handles it asynchronously
            resolve({ success: true, output: [] });
        });
    }

    clear(): void {
        this.queue.forEach(item => {
            item.reject(new Error('Queue cleared'));
        });
        this.queue = [];
    }

    get size(): number {
        return this.queue.length;
    }
}
