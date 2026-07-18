import { test } from 'node:test';
import * as assert from 'node:assert';
import { EventEmitter } from 'events';
import { MessageRouter } from '../../../dist/lib/messaging/message-router.js';

test('MessageRouter', async (t) => {
    await t.test('should route message to specific handler', async () => {
        const emitter = new EventEmitter();
        const router = new MessageRouter(emitter);
        
        let handled = false;
        router.registerHandler('test_topic', {
            handle: (msg) => {
                assert.strictEqual(msg.topic, 'test_topic');
                assert.deepStrictEqual(msg.content, { foo: 'bar' });
                handled = true;
            }
        });
        
        await router.route('test_topic|||{"foo":"bar"}');
        assert.ok(handled);
    });

    await t.test('should emit events', async () => {
        const emitter = new EventEmitter();
        const router = new MessageRouter(emitter);
        
        let wildcardEmitted = false;
        let specificEmitted = false;
        let messageEmitted = false;
        
        emitter.on('*', (topic, content) => {
            assert.strictEqual(topic, 'test_topic');
            assert.deepStrictEqual(content, { foo: 'bar' });
            wildcardEmitted = true;
        });
        
        emitter.on('test_topic', (content) => {
            assert.deepStrictEqual(content, { foo: 'bar' });
            specificEmitted = true;
        });
        
        emitter.on('message', (msg) => {
            assert.strictEqual(msg.topic, 'test_topic');
            messageEmitted = true;
        });
        
        await router.route('test_topic|||{"foo":"bar"}');
        
        assert.ok(wildcardEmitted);
        assert.ok(specificEmitted);
        assert.ok(messageEmitted);
    });
});
