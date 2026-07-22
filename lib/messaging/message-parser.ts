import type { JupyterMessage, MessageChannel, MessageTopic } from '../types/messages.js';

interface RawEnvelope {
    channel: MessageChannel;
    topic: string;
    msg_type: string;
    parent_msg_id: string;
    content: unknown;
}

export class MessageParser {
    static parse(raw: string): JupyterMessage {
        let envelope: RawEnvelope;
        try {
            envelope = JSON.parse(raw);
        } catch (error) {
            throw new Error(`Failed to parse message envelope: ${error}`);
        }

        if (!envelope || typeof envelope.msg_type !== 'string') {
            throw new Error(`Invalid message format: ${raw}`);
        }

        return {
            topic: envelope.topic as MessageTopic,
            msgType: envelope.msg_type,
            channel: envelope.channel,
            parentMsgId: envelope.parent_msg_id ?? '',
            content: envelope.content,
            timestamp: Date.now(),
            raw
        };
    }

    static stringify(message: JupyterMessage): string {
        return JSON.stringify({
            channel: message.channel,
            topic: message.topic,
            msg_type: message.msgType,
            parent_msg_id: message.parentMsgId,
            content: message.content
        });
    }
}
