import { test } from 'node:test';
import * as assert from 'node:assert';
import { MessageParser } from '../../../dist/lib/messaging/message-parser.js';

test('MessageParser', async (t) => {
    await t.test('should parse valid message', () => {
        const raw = 'stream|||{"name":"stdout","text":"hello"}';
        const parsed = MessageParser.parse(raw);
        
        assert.strictEqual(parsed.topic, 'stream');
        assert.deepStrictEqual(parsed.content, { name: 'stdout', text: 'hello' });
        assert.ok(parsed.timestamp > 0);
        assert.strictEqual(parsed.raw, raw);
    });

    await t.test('should throw on invalid format', () => {
        assert.throws(() => {
            MessageParser.parse('invalid-format');
        }, /Invalid message format/);
    });

    await t.test('should throw on invalid JSON', () => {
        assert.throws(() => {
            MessageParser.parse('stream|||{invalid-json}');
        }, /Failed to parse message content/);
    });

    await t.test('should stringify message', () => {
        const msg = {
            topic: 'stream',
            content: { name: 'stdout', text: 'hello' },
            timestamp: 123,
            raw: ''
        };
        
        const str = MessageParser.stringify(msg);
        assert.strictEqual(str, 'stream|||{"name":"stdout","text":"hello"}');
    });
});
