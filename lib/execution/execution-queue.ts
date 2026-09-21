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
    stopOnError: boolean;
    output: JupyterMessage[];
    timer: ReturnType<typeof setTimeout> | undefined;
    finish: (result: ExecutionResult) => void;
    reject: (error: Error) => void;
    // The kernel's `status: idle` for this request has arrived.
    idle: boolean;
    // Set once execute_reply has arrived while `idle` has not: finishes the
    // execution as soon as it does.
    finishWhenIdle: (() => void) | undefined;
}

const DEFAULT_TIMEOUT_MS = 30000;
// How long after execute_reply to wait for the kernel's `status: idle`, if it
// has not come yet. A kernel publishes it right after the reply, so this only
// matters for one that never does; it must not make such a kernel hang.
const DEFAULT_IDLE_WAIT_MS = 2000;
const ABORTED_MESSAGE = 'Execution aborted: an earlier execution failed with stopOnError';

export class ExecutionQueue {
    private queue: QueuedExecution[] = [];
    private executing: boolean = false;
    private addon: any;
    private maxSize: number;
    private pending: Map<string, PendingExecution> = new Map();
    private logger?: Logger;
    private onTimeout?: (msgId: string) => void;
    private idleWaitMs: number;

    // `onTimeout` is called when an execution times out (unless that
    // execution opted out with interruptOnTimeout: false), so the owner can
    // interrupt the kernel -- otherwise the kernel keeps running code nobody
    // is waiting for, and everything queued behind it (and every complete/
    // inspect request) is stuck behind it.
    constructor(addon: any, emitter: EventEmitter, maxSize: number = 100, logger?: Logger, onTimeout?: (msgId: string) => void, idleWaitMs: number = DEFAULT_IDLE_WAIT_MS) {
        this.addon = addon;
        this.onTimeout = onTimeout;
        this.idleWaitMs = idleWaitMs;
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
            msgId = this.addon.execute(item.code, item.options);
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
                const interrupting = item.options.interruptOnTimeout !== false && this.onTimeout !== undefined;
                if (interrupting) {
                    try {
                        this.onTimeout!(msgId);
                    } catch (error) {
                        this.logger?.error('Interrupting after a timeout failed', { error });
                    }
                }
                item.reject(new Error(`Execution timed out after ${timeoutMs}ms` + (interrupting ? ' (the kernel was interrupted)' : '')));
                this.processNext();
            }, timeoutMs)
            : undefined;

        this.pending.set(msgId, {
            stopOnError: item.options.stopOnError === true,
            output: [],
            idle: false,
            finishWhenIdle: undefined,
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
            case 'input_request':
                // The kernel is now genuinely blocked waiting on a human to
                // answer a prompt (see Session's own 'input_request' event/
                // sendInputReply()) -- there's no way to know how long that
                // will take, so the fixed execution timeout no longer
                // applies once this arrives. Without this, an execute()
                // with allowStdin: true could time out from nothing more
                // than a user taking longer than DEFAULT_TIMEOUT_MS to
                // answer a prompt, even though the kernel is behaving
                // completely normally and will resume the moment it gets a
                // reply.
                if (pending.timer) {
                    clearTimeout(pending.timer);
                    pending.timer = undefined;
                }
                break;

            case 'stream':
            case 'execute_result':
            case 'display_data':
            case 'update_display_data':
            case 'clear_output':
                pending.output.push(message);
                break;

            case 'status':
                // The kernel publishes this after everything else it published
                // for the request, on the same iopub socket, so once it has
                // arrived so has all of the output.
                if (message.content?.execution_state === 'idle') {
                    pending.idle = true;
                    pending.finishWhenIdle?.();
                }
                break;

            case 'error':
                pending.output.push(message);
                this.logger?.debug(`Execution ${message.parentMsgId} reported an R error`, { evalue: message.content?.evalue });
                if (pending.stopOnError) {
                    this.abortQueued();
                }
                pending.finish({
                    success: false,
                    status: 'error',
                    output: pending.output,
                    error: new Error(message.content?.evalue ?? 'R execution error')
                });
                break;

            case 'execute_reply': {
                const executionCount = message.content?.execution_count;
                const replyStatus = message.content?.status as 'ok' | 'error' | 'aborted' | undefined;
                let idleFallback: ReturnType<typeof setTimeout> | undefined;
                const finishNow = () => {
                    clearTimeout(idleFallback);
                    pending.finishWhenIdle = undefined;
                    if (replyStatus !== 'ok' && pending.stopOnError) {
                        this.abortQueued();
                    }
                    const result: ExecutionResult = {
                        success: replyStatus === 'ok',
                        output: pending.output,
                        executionCount
                    };
                    if (replyStatus) result.status = replyStatus;
                    if (replyStatus === 'aborted') {
                        result.aborted = true;
                        result.error = new Error(ABORTED_MESSAGE);
                    }
                    const userExpressions = message.content?.user_expressions;
                    if (userExpressions && Object.keys(userExpressions).length > 0) {
                        result.userExpressions = userExpressions;
                    }
                    pending.finish(result);
                };

                // iopub (stream/execute_result/display_data/error) and shell
                // (execute_reply) are separate ZMQ sockets with no cross-channel
                // delivery-order guarantee: this client can see the reply
                // before output the kernel published *before* it -- the tail of
                // a flood of print() calls, an input prompt's echo, the error
                // of an interrupted call. Confirmed on macOS CI runners, where
                // those went missing from the result.
                //
                // What the protocol does guarantee is that the kernel then
                // publishes `status: idle` on iopub, after all of the request's
                // output and on the same socket, so the execution is finished
                // once both the reply and that idle have arrived. A kernel that
                // never sends the idle costs idleWaitMs, not a hang.
                //
                // An aborted request (stop_on_error) never ran: the kernel sends
                // its reply and nothing else, no output and no idle.
                if (pending.idle || replyStatus === 'aborted') {
                    finishNow();
                } else {
                    pending.finishWhenIdle = finishNow;
                    idleFallback = setTimeout(() => {
                        if (this.pending.get(message.parentMsgId) === pending) {
                            this.logger?.warn(`Execution ${message.parentMsgId}: no idle status within ${this.idleWaitMs}ms of the reply; finishing without it`);
                            finishNow();
                        }
                    }, this.idleWaitMs);
                    idleFallback.unref?.();
                }
                break;
            }

            default:
                break;
        }
    }

    // stopOnError: an execution that failed takes everything still waiting
    // behind it down with it, unrun -- the client-side half of Jupyter's
    // stop_on_error (the kernel does the same to requests already queued on
    // ITS side, but this queue is single-flight, so anything behind the
    // failure is still here, never sent).
    private abortQueued(): void {
        if (this.queue.length === 0) {
            return;
        }
        this.logger?.warn(`Aborting ${this.queue.length} queued execution(s) after a failure (stopOnError)`);
        const aborted = this.queue;
        this.queue = [];
        for (const item of aborted) {
            item.resolve({
                success: false,
                status: 'aborted',
                aborted: true,
                output: [],
                error: new Error(ABORTED_MESSAGE)
            });
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
