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
        this.emitter.emit('*', message.topic, message.content);
        this.emitter.emit('message', message);
        
        // Route to specific handler
        const handler = this.findHandler(message.topic);
        if (handler) {
            await handler.handle(message, this.emitter);
        }
        
        // Always emit topic-specific event
        this.emitter.emit(message.topic, message.content);
    }

    private findHandler(topic: string): MessageHandler | undefined {
        // Exact match
        if (this.handlers.has(topic)) {
            return this.handlers.get(topic);
        }
        
        // Prefix match (e.g., "stream" matches "stream.stdout")
        for (const [key, handler] of this.handlers) {
            if (topic.startsWith(key)) {
                return handler;
            }
        }
        
        return undefined;
    }

    registerHandler(topic: string, handler: MessageHandler): void {
        this.handlers.set(topic, handler);
    }
}
