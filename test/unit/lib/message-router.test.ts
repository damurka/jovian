import { test } from 'node:test';
import * as assert from 'node:assert';
import { EventEmitter } from 'events';
import { MessageRouter } from '../../../dist/lib/messaging/message-router.js';

function envelope(msgType: string, content: unknown): string {
    return JSON.stringify({
        channel: 'iopub',
        topic: `kernel_core.abc.${msgType}`,
        msg_type: msgType,
        parent_msg_id: 'parent-1',
        content
    });
}

test('MessageRouter', async (t) => {
    await t.test('should route message to specific handler by msg_type', async () => {
        const emitter = new EventEmitter();
        const router = new MessageRouter(emitter);

        let handled = false;
        router.registerHandler('test_type', {
            handle: (msg) => {
                assert.strictEqual(msg.msgType, 'test_type');
                assert.deepStrictEqual(msg.content, { foo: 'bar' });
                handled = true;
            }
        });

        await router.route(envelope('test_type', { foo: 'bar' }));
        assert.ok(handled);
    });

    await t.test('should emit events', async () => {
        const emitter = new EventEmitter();
        const router = new MessageRouter(emitter);

        let wildcardEmitted = false;
        let specificEmitted = false;
        let messageEmitted = false;

        emitter.on('*', (msgType, content) => {
            assert.strictEqual(msgType, 'test_type');
            assert.deepStrictEqual(content, { foo: 'bar' });
            wildcardEmitted = true;
        });

        emitter.on('test_type', (content) => {
            assert.deepStrictEqual(content, { foo: 'bar' });
            specificEmitted = true;
        });

        emitter.on('message', (msg) => {
            assert.strictEqual(msg.msgType, 'test_type');
            assert.strictEqual(msg.parentMsgId, 'parent-1');
            messageEmitted = true;
        });

        await router.route(envelope('test_type', { foo: 'bar' }));

        assert.ok(wildcardEmitted);
        assert.ok(specificEmitted);
        assert.ok(messageEmitted);
    });
});
