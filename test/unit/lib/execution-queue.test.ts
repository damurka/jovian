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

    await t.test('should still collect output that arrives just after an ok execute_reply', async () => {
        // Regression test for a real, confirmed-via-CI flake: iopub
        // (execute_result) and shell (execute_reply) are separate channels
        // with no cross-channel delivery-order guarantee, so a passing
        // execute_reply can arrive at this client before the iopub message
        // it depends on, even though the kernel always publishes iopub
        // content first. See handleMessage()'s 'execute_reply' case.
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-race' };

        const queue = new ExecutionQueue(mockAddon, emitter);
        const resultPromise = queue.execute('2');

        // execute_reply arrives first, with nothing collected yet...
        emitter.emit('message', message('execute_reply', 'msg-race', { status: 'ok', execution_count: 1 }));
        // ...then the execute_result iopub message arrives microseconds later.
        emitter.emit('message', message('execute_result', 'msg-race', { data: { 'text/plain': '2' } }));

        const result = await resultPromise;
        assert.strictEqual(result.success, true);
        assert.strictEqual(result.output.length, 1);
        assert.strictEqual(result.output[0].content.data['text/plain'], '2');
    });

    await t.test('should resolve promptly with empty output when nothing ever arrives after an ok reply', async () => {
        // The grace period added for the race above must not turn a
        // genuinely-empty-output execution (e.g. a bare assignment) into a
        // slow one it isn't already -- it should still resolve once the
        // grace period elapses, not hang.
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-empty' };

        const queue = new ExecutionQueue(mockAddon, emitter);
        const resultPromise = queue.execute('x <- 1');
        emitter.emit('message', message('execute_reply', 'msg-empty', { status: 'ok', execution_count: 1 }));

        const result = await resultPromise;
        assert.strictEqual(result.success, true);
        assert.strictEqual(result.output.length, 0);
    });

    await t.test('should not double-resolve if the queue is cleared during the post-reply grace period', async () => {
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-cleared' };

        const queue = new ExecutionQueue(mockAddon, emitter);
        const resultPromise = queue.execute('1');
        emitter.emit('message', message('execute_reply', 'msg-cleared', { status: 'ok', execution_count: 1 }));

        // Cleared while the grace-period timer is still pending -- must
        // reject (from clear()), not later also resolve once the timer fires.
        queue.clear();
        await assert.rejects(resultPromise, /Queue cleared/);

        // Let the grace-period timer actually elapse to confirm it's a
        // harmless no-op (an already-settled promise can't change outcome
        // either way, but this exercises the has()-guard path directly).
        await new Promise((resolve) => setTimeout(resolve, 60));
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

    await t.test('should forward execute()\'s options (e.g. allowStdin) to the addon', async () => {
        // Regression test: processNext() used to call this.addon.execute(
        // item.code) with no second argument at all, so allowStdin/silent/
        // storeHistory never reached the WS execute frame no matter what a
        // caller passed to execute() -- see Session's wsAddon.execute() in
        // lib/session/session-manager.ts, which is the addon this stands in for.
        const emitter = new EventEmitter();
        let receivedOptions: unknown;
        const mockAddon = {
            execute: (_code: string, options: unknown) => {
                receivedOptions = options;
                return 'msg-opts';
            }
        };

        const queue = new ExecutionQueue(mockAddon, emitter);
        const resultPromise = queue.execute('input("x?")', { allowStdin: true, silent: true });
        emitter.emit('message', message('execute_reply', 'msg-opts', { status: 'ok' }));
        await resultPromise;

        assert.deepStrictEqual(receivedOptions, { allowStdin: true, silent: true });
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

    await t.test('should not time out while blocked on an input_request, even past the configured timeout', async () => {
        // Regression test for a real, reported problem: the playground's
        // execute timeout used to apply even while a kernel was genuinely,
        // healthily blocked waiting on a human to answer an input() prompt
        // -- a slow human, not a stuck kernel, could trip it. An
        // input_request must clear the pending timer so no fixed deadline
        // applies once the kernel is legitimately waiting on stdin.
        const emitter = new EventEmitter();
        const mockAddon = { execute: () => 'msg-input' };
        const queue = new ExecutionQueue(mockAddon, emitter, 100, undefined);

        const resultPromise = queue.execute('input("x?")', { timeout: 20, allowStdin: true });
        emitter.emit('message', message('input_request', 'msg-input', { prompt: 'x?', password: false }));

        // Long past the 20ms timeout that would otherwise have fired.
        await new Promise((resolve) => setTimeout(resolve, 60));
        emitter.emit('message', message('execute_reply', 'msg-input', { status: 'ok', execution_count: 1 }));

        const result = await resultPromise;
        assert.strictEqual(result.success, true);
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
