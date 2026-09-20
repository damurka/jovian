import type { MessageHandler } from '../messaging/message-router.js';
import type { JupyterMessage, StreamContent } from '../types/messages.js';
import { EventEmitter } from 'events';

export class StreamHandler implements MessageHandler {
    handle(message: JupyterMessage<StreamContent>, emitter: EventEmitter): void {
        if (message.content && message.content.text) {
            emitter.emit(message.content.name === 'stderr' ? 'stderr' : 'stdout', message.content.text);
        }
    }
}
