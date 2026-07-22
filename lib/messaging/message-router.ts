import { EventEmitter } from 'events';
import type { JupyterMessage } from '../types/messages.js';
import { MessageParser } from './message-parser.js';

export interface MessageHandler {
    handle(message: JupyterMessage, emitter: EventEmitter): Promise<void> | void;
}

export class MessageRouter {
    private handlers: Map<string, MessageHandler>;
    private emitter: EventEmitter;

    constructor(emitter: EventEmitter) {
        this.emitter = emitter;
        this.handlers = new Map();
    }

    async route(rawMessage: string): Promise<void> {
        const message = MessageParser.parse(rawMessage);

        // Emit raw message event
        this.emitter.emit('*', message.msgType, message.content);
        this.emitter.emit('message', message);

        // Route to specific handler (dispatch on the plain msg_type, e.g.
        // "stream" / "execute_result" — `topic` is a kernel-namespaced
        // string like "kernel_core.<id>.stream" and isn't matched here)
        const handler = this.findHandler(message.msgType);
        if (handler) {
            await handler.handle(message, this.emitter);
        }

        // Always emit msg_type-specific event
        this.emitter.emit(message.msgType, message.content);
    }

    private findHandler(msgType: string): MessageHandler | undefined {
        // Exact match
        if (this.handlers.has(msgType)) {
            return this.handlers.get(msgType);
        }

        // Prefix match (e.g., "stream" matches "stream.stdout")
        for (const [key, handler] of this.handlers) {
            if (msgType.startsWith(key)) {
                return handler;
            }
        }

        return undefined;
    }

    registerHandler(msgType: string, handler: MessageHandler): void {
        this.handlers.set(msgType, handler);
    }
}
