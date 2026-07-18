import type { JupyterMessage, MessageTopic } from '../types/messages.js';

export class MessageParser {
    static parse(raw: string): JupyterMessage {
        const [topic, contentJson] = raw.split('|||');
        
        if (!topic || !contentJson) {
            throw new Error(`Invalid message format: ${raw}`);
        }

        try {
            const content = JSON.parse(contentJson);
            return {
                topic: topic as MessageTopic,
                content,
                timestamp: Date.now(),
                raw
            };
        } catch (error) {
            throw new Error(`Failed to parse message content: ${error}`);
        }
    }

    static stringify(message: JupyterMessage): string {
        return `${message.topic}|||${JSON.stringify(message.content)}`;
    }
}
