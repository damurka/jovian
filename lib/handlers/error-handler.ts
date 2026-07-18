import type { MessageHandler } from '../messaging/message-router.js';
import type { JupyterMessage, ErrorContent } from '../types/messages.js';
import { EventEmitter } from 'events';

export class ErrorHandler implements MessageHandler {
    handle(message: JupyterMessage<ErrorContent>, emitter: EventEmitter): void {
        if (message.content && message.content.evalue) {
            emitter.emit('error', message.content.evalue);
        }
    }
}
