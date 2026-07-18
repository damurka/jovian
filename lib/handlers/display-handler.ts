import type { MessageHandler } from '../messaging/message-router.js';
import type { JupyterMessage, DisplayDataContent } from '../types/messages.js';
import { EventEmitter } from 'events';

export class DisplayHandler implements MessageHandler {
    handle(message: JupyterMessage<DisplayDataContent>, emitter: EventEmitter): void {
        if (message.content && message.content.data && message.content.data['text/plain']) {
            const textPlain = message.content.data['text/plain'];
            const resultText = Array.isArray(textPlain) ? textPlain.join('\n') : textPlain;
            emitter.emit('result', resultText);
        }
    }
}
