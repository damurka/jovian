import { EventEmitter } from 'events';

/**
 * The parts of a Session a Comm needs -- kept as an interface so this file
 * doesn't import session-manager (which imports it).
 */
export interface CommTransport {
    commMsg(commId: string, data: Record<string, unknown>): Promise<string>;
    commClose(commId: string, data: Record<string, unknown>): Promise<string>;
}

/**
 * One open comm: a named, bidirectional message channel between this client
 * and a target registered inside the kernel (in R, `hera::CommManager$
 * register_comm_target()`).
 *
 * Get one either from `session.openComm(target)` (client-initiated) or from
 * the session's `'comm'` event (kernel-initiated, e.g. R calling
 * `CommManager$new_comm()` then `$open()`).
 *
 * Events: `'message'` (data) for every comm_msg the kernel sends over it and
 * `'close'` (data) once it is closed by either side -- or because the kernel
 * restarted or the session ended, in which case `data.reason` says so.
 */
export class Comm extends EventEmitter {
    private isClosed = false;

    constructor(
        readonly id: string,
        readonly targetName: string,
        private readonly transport: CommTransport
    ) {
        super();
    }

    get closed(): boolean {
        return this.isClosed;
    }

    /** Sends `data` to the kernel-side handler. Rejects if the comm is closed. */
    send(data: Record<string, unknown> = {}): Promise<string> {
        if (this.isClosed) {
            return Promise.reject(new Error(`Comm ${this.id} (${this.targetName}) is closed`));
        }
        return this.transport.commMsg(this.id, data);
    }

    /** Closes the comm (the kernel is told; no-op if it is already closed). */
    async close(data: Record<string, unknown> = {}): Promise<void> {
        if (this.isClosed) {
            return;
        }
        this.isClosed = true;
        await this.transport.commClose(this.id, data);
        this.emit('close', data);
    }

    /** @internal Called by Session for the kernel's own comm_msg / comm_close. */
    receiveMessage(data: Record<string, unknown>): void {
        this.emit('message', data);
    }

    /** @internal */
    receiveClose(data: Record<string, unknown>): void {
        if (this.isClosed) {
            return;
        }
        this.isClosed = true;
        this.emit('close', data);
    }
}
