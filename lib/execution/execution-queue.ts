import type { EventEmitter } from 'events';
import type { ExecutionOptions, ExecutionResult } from '../types/index.js';
import type { JupyterMessage } from '../types/messages.js';

interface QueuedExecution {
    code: string;
    options: ExecutionOptions;
    resolve: (result: ExecutionResult) => void;
    reject: (error: Error) => void;
    timestamp: number;
}

interface PendingExecution {
    output: JupyterMessage[];
    timer: ReturnType<typeof setTimeout> | undefined;
    finish: (result: ExecutionResult) => void;
    reject: (error: Error) => void;
}

const DEFAULT_TIMEOUT_MS = 30000;

export class ExecutionQueue {
    private queue: QueuedExecution[] = [];
    private executing: boolean = false;
    private addon: any;
    private maxSize: number;
    private pending: Map<string, PendingExecution> = new Map();

    constructor(addon: any, emitter: EventEmitter, maxSize: number = 100) {
        this.addon = addon;
        this.maxSize = maxSize;
        emitter.on('message', (message: JupyterMessage) => this.handleMessage(message));
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

    private processNext(): void {
        if (this.queue.length === 0) {
            this.executing = false;
            return;
        }

        this.executing = true;
        const item = this.queue.shift()!;

        let msgId: string;
        try {
            msgId = this.addon.execute(item.code);
        } catch (error) {
            item.reject(error as Error);
            this.processNext();
            return;
        }

        if (!msgId) {
            item.reject(new Error('Native addon did not return a message id for this execution'));
            this.processNext();
            return;
        }

        // A timeout of 0 means "no timeout" -- used for long-running calls
        // that intentionally block the R session until something external
        // stops them (e.g. shiny::runApp(), see DatasuiteEngine.createShiny).
        // Note this also means no *further* queued execute() calls will be
        // sent until this one's execute_reply arrives, since R itself is
        // single-threaded and busy running it.
        const timeoutMs = item.options.timeout ?? DEFAULT_TIMEOUT_MS;
        const timer = timeoutMs > 0
            ? setTimeout(() => {
                this.pending.delete(msgId);
                item.reject(new Error(`Execution timed out after ${timeoutMs}ms`));
                this.processNext();
            }, timeoutMs)
            : undefined;

        this.pending.set(msgId, {
            output: [],
            timer,
            finish: (result) => {
                clearTimeout(timer);
                this.pending.delete(msgId);
                item.resolve(result);
                this.processNext();
            },
            reject: (error) => {
                clearTimeout(timer);
                this.pending.delete(msgId);
                item.reject(error);
            }
        });
    }

    private handleMessage(message: JupyterMessage): void {
        const pending = this.pending.get(message.parentMsgId);
        if (!pending) {
            return;
        }

        switch (message.msgType) {
            case 'stream':
            case 'execute_result':
            case 'display_data':
                pending.output.push(message);
                break;

            case 'error':
                pending.output.push(message);
                pending.finish({
                    success: false,
                    output: pending.output,
                    error: new Error(message.content?.evalue ?? 'R execution error')
                });
                break;

            case 'execute_reply':
                pending.finish({
                    success: message.content?.status === 'ok',
                    output: pending.output,
                    executionCount: message.content?.execution_count
                });
                break;

            default:
                break;
        }
    }

    clear(): void {
        this.queue.forEach(item => {
            item.reject(new Error('Queue cleared'));
        });
        this.queue = [];
        this.executing = false;

        // Reject in-flight executions too, so callers never end up with a
        // promise that silently hangs forever (or rejects long after the
        // caller stopped caring, once its timeout eventually fires).
        this.pending.forEach(pending => pending.reject(new Error('Queue cleared')));
    }

    get size(): number {
        return this.queue.length;
    }
}
