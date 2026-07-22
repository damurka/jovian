import { test } from 'node:test';
import * as assert from 'node:assert';
import { MessageParser } from '../../../dist/lib/messaging/message-parser.js';

function envelope(overrides: Record<string, unknown> = {}): string {
    return JSON.stringify({
        channel: 'iopub',
        topic: 'kernel_core.abc.stream',
        msg_type: 'stream',
        parent_msg_id: 'parent-1',
        content: { name: 'stdout', text: 'hello' },
        ...overrides
    });
}

test('MessageParser', async (t) => {
    await t.test('should parse valid message', () => {
        const raw = envelope();
        const parsed = MessageParser.parse(raw);

        assert.strictEqual(parsed.topic, 'kernel_core.abc.stream');
        assert.strictEqual(parsed.msgType, 'stream');
        assert.strictEqual(parsed.channel, 'iopub');
        assert.strictEqual(parsed.parentMsgId, 'parent-1');
        assert.deepStrictEqual(parsed.content, { name: 'stdout', text: 'hello' });
        assert.ok(parsed.timestamp > 0);
        assert.strictEqual(parsed.raw, raw);
    });

    await t.test('should throw on invalid format', () => {
        assert.throws(() => {
            MessageParser.parse('not-json');
        }, /Failed to parse message envelope/);
    });

    await t.test('should throw when msg_type is missing', () => {
        assert.throws(() => {
            MessageParser.parse(JSON.stringify({ topic: 'stream', content: {} }));
        }, /Invalid message format/);
    });

    await t.test('should stringify message', () => {
        const msg = {
            topic: 'kernel_core.abc.stream',
            msgType: 'stream',
            channel: 'iopub' as const,
            parentMsgId: 'parent-1',
            content: { name: 'stdout', text: 'hello' },
            timestamp: 123,
            raw: ''
        };

        const str = MessageParser.stringify(msg);
        assert.deepStrictEqual(JSON.parse(str), {
            channel: 'iopub',
            topic: 'kernel_core.abc.stream',
            msg_type: 'stream',
            parent_msg_id: 'parent-1',
            content: { name: 'stdout', text: 'hello' }
        });
    });
});
