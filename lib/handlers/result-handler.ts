import type { MessageHandler } from '../messaging/message-router.js';
import type { JupyterMessage, ExecuteResultContent } from '../types/messages.js';
import { EventEmitter } from 'events';

export class ResultHandler implements MessageHandler {
    handle(message: JupyterMessage<ExecuteResultContent>, emitter: EventEmitter): void {
        if (message.content && message.content.data && message.content.data['text/plain']) {
            const textPlain = message.content.data['text/plain'];
            const resultText = Array.isArray(textPlain) ? textPlain.join('\n') : textPlain;
            emitter.emit('result', resultText);
        }
    }
}
