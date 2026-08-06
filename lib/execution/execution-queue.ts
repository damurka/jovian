import type { EventEmitter } from 'events';
import type { ExecutionOptions, ExecutionResult } from '../types/index.js';
import type { JupyterMessage } from '../types/messages.js';
import type { Logger } from '../utils/logger.js';

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
    private logger?: Logger;

    constructor(addon: any, emitter: EventEmitter, maxSize: number = 100, logger?: Logger) {
        this.addon = addon;
        this.maxSize = maxSize;
        this.logger = logger;
        emitter.on('message', (message: JupyterMessage) => this.handleMessage(message));
    }

    execute(code: string, options: ExecutionOptions = {}): Promise<ExecutionResult> {
        return new Promise((resolve, reject) => {
            if (this.queue.length >= this.maxSize) {
                this.logger?.error(`Execution queue is full (maxSize=${this.maxSize}); rejecting new request`);
                reject(new Error('Execution queue is full'));
                return;
            }

            this.logger?.trace(`Queued execution (queue depth: ${this.queue.length + 1})`, { timeout: options.timeout });

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
        const queuedMs = Date.now() - item.timestamp;

        let msgId: string;
        try {
            msgId = this.addon.execute(item.code);
        } catch (error) {
            this.logger?.error('Native addon threw while starting execution', { error, queuedMs });
            item.reject(error as Error);
            this.processNext();
            return;
        }

        if (!msgId) {
            this.logger?.error('Native addon did not return a message id for this execution', { queuedMs });
            item.reject(new Error('Native addon did not return a message id for this execution'));
            this.processNext();
            return;
        }

        this.logger?.trace(`Execution ${msgId} started after ${queuedMs}ms in queue`);

        // A timeout of 0 means "no timeout" -- used for long-running calls
        // that intentionally block the R session until something external
        // stops them (e.g. shiny::runApp(), see Session.createShiny in
        // lib/session/session-manager.ts).
        // Note this also means no *further* queued execute() calls will be
        // sent until this one's execute_reply arrives, since R itself is
        // single-threaded and busy running it.
        const timeoutMs = item.options.timeout ?? DEFAULT_TIMEOUT_MS;
        const timer = timeoutMs > 0
            ? setTimeout(() => {
                this.logger?.error(`Execution ${msgId} timed out after ${timeoutMs}ms`, { code: previewCode(item.code) });
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
                this.logger?.trace(`Execution ${msgId} finished`, { success: result.success, outputMessages: result.output?.length ?? 0 });
                item.resolve(result);
                this.processNext();
            },
            reject: (error) => {
                clearTimeout(timer);
                this.pending.delete(msgId);
                this.logger?.error(`Execution ${msgId} rejected`, { error });
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
                this.logger?.error(`Execution ${message.parentMsgId} reported an R error`, { evalue: message.content?.evalue });
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
        if (this.queue.length > 0 || this.pending.size > 0) {
            this.logger?.warn(`Clearing execution queue (${this.queue.length} queued, ${this.pending.size} in flight)`);
        }

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

function previewCode(code: string, maxLength: number = 200): string {
    const singleLine = code.replace(/\s+/g, ' ').trim();
    return singleLine.length > maxLength ? `${singleLine.slice(0, maxLength)}…` : singleLine;
}
