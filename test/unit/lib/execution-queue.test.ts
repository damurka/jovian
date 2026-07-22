import { test } from 'node:test';
import * as assert from 'node:assert';
import { EventEmitter } from 'events';
import { ExecutionQueue } from '../../../dist/lib/execution/execution-queue.js';

function message(msgType: string, parentMsgId: string, content: unknown) {
    return { topic: msgType, msgType, channel: 'iopub', parentMsgId, content, timestamp: Date.now(), raw: '' };
}

test('ExecutionQueue', async (t) => {
    await t.test('should resolve with real output once execute_reply arrives', async () => {
        const emitter = new EventEmitter();
        let nextMsgId = 0;
        const mockAddon = {
            execute: () => `msg-${++nextMsgId}`
        };

        const queue = new ExecutionQueue(mockAddon, emitter);
        const resultPromise = queue.execute('1 + 1');

        emitter.emit('message', message('stream', 'msg-1', { name: 'stdout', text: 'hello\n' }));
        emitter.emit('message', message('execute_reply', 'msg-1', { status: 'ok', execution_count: 1 }));

        const result = await resultPromise;
        assert.strictEqual(result.success, true);
        assert.strictEqual(result.executionCount, 1);
        assert.strictEqual(result.output.length, 1);
        assert.strictEqual(result.output[0].content.text, 'hello\n');
    });

    await t.test('should reject when kernel reports an error', async () => {
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-err' };

        const queue = new ExecutionQueue(mockAddon, emitter);
        const resultPromise = queue.execute('stop("boom")');

        emitter.emit('message', message('error', 'msg-err', { ename: 'Error', evalue: 'boom', traceback: [] }));

        const result = await resultPromise;
        assert.strictEqual(result.success, false);
        assert.strictEqual(result.error?.message, 'boom');
    });

    await t.test('should process queued executions sequentially, one in flight at a time', async () => {
        const emitter = new EventEmitter();
        const executed: string[] = [];
        let nextMsgId = 0;
        const mockAddon = {
            execute: (code: string) => {
                executed.push(code);
                return `msg-${++nextMsgId}`;
            }
        };

        const queue = new ExecutionQueue(mockAddon, emitter);

        const p1 = queue.execute('code1');
        const p2 = queue.execute('code2');

        // Only the first item should have been sent to the addon so far.
        assert.deepStrictEqual(executed, ['code1']);

        emitter.emit('message', message('execute_reply', 'msg-1', { status: 'ok' }));
        await p1;

        assert.deepStrictEqual(executed, ['code1', 'code2']);
        emitter.emit('message', message('execute_reply', 'msg-2', { status: 'ok' }));
        await p2;
    });

    await t.test('should reject on timeout when no reply arrives', async () => {
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-timeout' };
        const queue = new ExecutionQueue(mockAddon, emitter);

        await assert.rejects(
            queue.execute('Sys.sleep(100)', { timeout: 20 }),
            /timed out/
        );
    });

    await t.test('should reject when queue is full', async () => {
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-x' };
        const queue = new ExecutionQueue(mockAddon, emitter, 1); // Max size 1

        const p1 = queue.execute('code1'); // dispatched immediately, now in flight
        const p2 = queue.execute('code2'); // fills the queue

        await assert.rejects(
            queue.execute('code3'),
            /Execution queue is full/
        );

        // Clean up the in-flight + queued executions so they don't leave
        // dangling timers/unhandled rejections after the test finishes.
        queue.clear();
        await assert.rejects(p1, /Queue cleared/);
        await assert.rejects(p2, /Queue cleared/);
    });

    await t.test('should clear queue', async () => {
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-x' };
        const queue = new ExecutionQueue(mockAddon, emitter);

        const p1 = queue.execute('code1'); // dispatched immediately, now in flight
        const p2 = queue.execute('code2');

        queue.clear();

        assert.strictEqual(queue.size, 0);
        await assert.rejects(p1, /Queue cleared/);
        await assert.rejects(p2, /Queue cleared/);
    });
});
