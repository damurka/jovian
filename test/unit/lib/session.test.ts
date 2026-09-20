import { test } from 'node:test';
import * as assert from 'node:assert';
import { Session } from '../../../dist/lib/session/session-manager.js';

// Minimal stand-in for the browser/undici WebSocket API session-manager.ts
// actually uses (addEventListener/removeEventListener/send/close) -- lets
// Session be exercised without a real supervisor process, the same way
// execution-queue.test.ts exercises ExecutionQueue with a mock addon object
// instead of the real native addon.
class FakeWebSocket {
    static instances: FakeWebSocket[] = [];
    url: string;
    sent: string[] = [];
    closed = false;
    private listeners = new Map<string, Set<(event: any) => void>>();

    constructor(url: string) {
        this.url = url;
        FakeWebSocket.instances.push(this);
    }

    addEventListener(type: string, cb: (event: any) => void): void {
        if (!this.listeners.has(type)) {
            this.listeners.set(type, new Set());
        }
        this.listeners.get(type)!.add(cb);
    }

    removeEventListener(type: string, cb: (event: any) => void): void {
        this.listeners.get(type)?.delete(cb);
    }

    send(data: string): void {
        this.sent.push(data);
    }

    close(): void {
        if (this.closed) return;
        this.closed = true;
        this.dispatchEvent('close', {});
    }

    dispatchEvent(type: string, event: any): void {
        for (const cb of [...(this.listeners.get(type) ?? [])]) {
            cb(event);
        }
    }

    /** Test helper: simulate a text frame arriving from the supervisor. */
    receive(payload: unknown): void {
        this.dispatchEvent('message', { data: JSON.stringify(payload) });
    }

    /** Test helper: pull the `id` off the most recently sent frame of a given type. */
    lastSentId(type: string): string {
        const frame = [...this.sent].reverse()
            .map((s) => JSON.parse(s))
            .find((f) => f.type === type);
        assert.ok(frame, `expected a sent frame of type "${type}"`);
        return frame.id;
    }
}

const fakeSupervisor: any = {
    stopSession: async () => {}
};

async function withFakeWebSocket<T>(fn: () => Promise<T>): Promise<T> {
    const original = (globalThis as any).WebSocket;
    (globalThis as any).WebSocket = FakeWebSocket;
    FakeWebSocket.instances = [];
    try {
        return await fn();
    } finally {
        (globalThis as any).WebSocket = original;
    }
}

// execute()/createShiny() start with `await this.readyPromise` -- even
// though that promise is already settled by the time these tests call
// them, `await` on an already-resolved promise still defers to a later
// tick rather than continuing synchronously. Tests that need to observe
// what execute() sends (or that need it to have registered as pending
// before simulating a disconnect) must wait for that tick first.
function flushMicrotasks(): Promise<void> {
    return new Promise((resolve) => setImmediate(resolve));
}

function connectionInfo() {
    return { sessionId: 'session-1', httpBase: 'http://127.0.0.1:0', wsBase: 'ws://127.0.0.1:0' };
}

test('Session', async (t) => {
    await t.test('ready() resolves once a ready frame arrives', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];

            let resolved = false;
            const readyPromise = session.ready().then(() => { resolved = true; });

            assert.strictEqual(resolved, false);
            ws.receive({ type: 'ready' });
            await readyPromise;
            assert.strictEqual(resolved, true);
        });
    });

    await t.test('execute() sends an execute frame and resolves once execute_reply arrives', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const resultPromise = session.execute('1 + 1');
            await flushMicrotasks();
            const id = ws.lastSentId('execute');

            ws.receive({
                type: 'message', channel: 'iopub', topic: 'stream',
                msg_type: 'stream', parent_msg_id: id, content: { name: 'stdout', text: '2\n' }
            });
            ws.receive({
                type: 'message', channel: 'shell', topic: 'execute_reply',
                msg_type: 'execute_reply', parent_msg_id: id, content: { status: 'ok', execution_count: 1 }
            });

            const result = await resultPromise;
            assert.strictEqual(result.success, true);
            assert.strictEqual(result.executionCount, 1);
            assert.strictEqual(result.output[0].content.text, '2\n');
        });
    });

    await t.test('execute() forwards its options (e.g. allowStdin) in the sent execute frame', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            void session.execute('input("name? ")', { allowStdin: true, silent: true, storeHistory: false });
            await flushMicrotasks();

            const frame = [...ws.sent].map((s) => JSON.parse(s)).reverse().find((f) => f.type === 'execute');
            assert.ok(frame, 'expected a sent frame of type "execute"');
            assert.deepStrictEqual(frame.options, { allowStdin: true, silent: true, storeHistory: false });
        });
    });

    await t.test('emits "input_request" for a stdin input_request message, and sendInputReply() answers it', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const resultPromise = session.execute('input("name? ")', { allowStdin: true });
            await flushMicrotasks();
            const id = ws.lastSentId('execute');

            let receivedPrompt: { prompt: string; password: boolean } | undefined;
            session.on('input_request', (content: { prompt: string; password: boolean }) => {
                receivedPrompt = content;
            });

            ws.receive({
                type: 'message', channel: 'stdin', topic: 'input_request',
                msg_type: 'input_request', parent_msg_id: id, content: { prompt: 'name? ', password: false }
            });
            await flushMicrotasks();

            assert.deepStrictEqual(receivedPrompt, { prompt: 'name? ', password: false });

            session.sendInputReply('David');
            const replyFrame = [...ws.sent].map((s) => JSON.parse(s)).reverse().find((f) => f.type === 'inputReply');
            assert.deepStrictEqual(replyFrame, { type: 'inputReply', value: 'David' });

            ws.receive({
                type: 'message', channel: 'shell', topic: 'execute_reply',
                msg_type: 'execute_reply', parent_msg_id: id, content: { status: 'ok', execution_count: 1 }
            });
            const result = await resultPromise;
            assert.strictEqual(result.success, true);
        });
    });

    await t.test('emits "stdout" for stream messages', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const stdout: string[] = [];
            session.on('stdout', (text: string) => stdout.push(text));

            const resultPromise = session.execute('print(1)');
            await flushMicrotasks();
            const id = ws.lastSentId('execute');
            ws.receive({
                type: 'message', channel: 'iopub', topic: 'stream',
                msg_type: 'stream', parent_msg_id: id, content: { name: 'stdout', text: 'hello\n' }
            });
            ws.receive({
                type: 'message', channel: 'shell', topic: 'execute_reply',
                msg_type: 'execute_reply', parent_msg_id: id, content: { status: 'ok', execution_count: 1 }
            });
            await resultPromise;

            assert.deepStrictEqual(stdout, ['hello\n']);
        });
    });

    await t.test('rejects pending execute() and emits "exit" when the connection closes unexpectedly', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            let exitEmitted = false;
            session.on('exit', () => { exitEmitted = true; });

            const resultPromise = session.execute('Sys.sleep(100)');
            await flushMicrotasks();
            ws.close();

            await assert.rejects(resultPromise, /exited unexpectedly|closed unexpectedly|Queue cleared/);
            assert.strictEqual(exitEmitted, true);
        });
    });

    await t.test('a "kernelExit" frame carries its reason through to the "exit" event', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            let exitInfo: { reason?: string } | undefined;
            session.on('exit', (info: { reason?: string }) => { exitInfo = info; });

            ws.receive({
                type: 'kernelExit',
                reason: 'heartbeat gave up waiting for a response (process exited with code 0xc0000005 (STATUS_ACCESS_VIOLATION -- a native crash, e.g. in a compiled R package))'
            });

            assert.strictEqual(
                exitInfo?.reason,
                'heartbeat gave up waiting for a response (process exited with code 0xc0000005 (STATUS_ACCESS_VIOLATION -- a native crash, e.g. in a compiled R package))'
            );
        });
    });

    await t.test('a graceful stop() does not emit "exit" (only unexpected closes do)', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            let exitEmitted = false;
            session.on('exit', () => { exitEmitted = true; });

            await session.stop();
            assert.strictEqual(ws.closed, true);
            // A close after an intentional stop() must not be treated as a crash.
            assert.strictEqual(exitEmitted, false);
        });
    });

    await t.test('exposes the options it was created with', async () => {
        await withFakeWebSocket(async () => {
            const options = { kernelType: 'python' as const, pythonHome: '/opt/python' };
            const session = new Session(connectionInfo(), options, fakeSupervisor);
            assert.strictEqual(session.options, options);
        });
    });

    await t.test('getHistory() records each execution\'s code and its iopub messages', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const resultPromise = session.execute('print(42)');
            await flushMicrotasks();
            const id = ws.lastSentId('execute');

            ws.receive({
                type: 'message', channel: 'iopub', topic: 'execute_input',
                msg_type: 'execute_input', parent_msg_id: id, content: { code: 'print(42)', execution_count: 1 }
            });
            ws.receive({
                type: 'message', channel: 'iopub', topic: 'stream',
                msg_type: 'stream', parent_msg_id: id, content: { name: 'stdout', text: '42\n' }
            });
            ws.receive({
                type: 'message', channel: 'shell', topic: 'execute_reply',
                msg_type: 'execute_reply', parent_msg_id: id, content: { status: 'ok', execution_count: 1 }
            });
            await resultPromise;

            const history = session.getHistory();
            assert.strictEqual(history.length, 1);
            assert.strictEqual(history[0].code, 'print(42)');
            assert.strictEqual(history[0].executionCount, 1);
            assert.deepStrictEqual(
                history[0].messages.map((m) => m.msgType),
                ['stream', 'execute_reply']
            );
        });
    });

    await t.test('getHistory() drops a stale input_request rather than recording it', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            void session.execute('input("x?")', { allowStdin: true });
            await flushMicrotasks();
            const id = ws.lastSentId('execute');

            ws.receive({
                type: 'message', channel: 'iopub', topic: 'execute_input',
                msg_type: 'execute_input', parent_msg_id: id, content: { code: 'input("x?")', execution_count: 1 }
            });
            ws.receive({
                type: 'message', channel: 'stdin', topic: 'input_request',
                msg_type: 'input_request', parent_msg_id: id, content: { prompt: 'x?', password: false }
            });
            await flushMicrotasks();

            assert.deepStrictEqual(session.getHistory()[0].messages, []);
        });
    });

    await t.test('queryKernelHistory() sends a history frame and resolves with the reply', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const historyPromise = session.queryKernelHistory({ n: 5 });
            await flushMicrotasks();

            const sentFrame = [...ws.sent].map((s) => JSON.parse(s)).reverse().find((f) => f.type === 'history');
            assert.ok(sentFrame, 'expected a sent frame of type "history"');
            assert.deepStrictEqual(sentFrame.options, { n: 5 });

            ws.receive({
                type: 'message', channel: 'shell', topic: 'history_reply',
                msg_type: 'history_reply', parent_msg_id: sentFrame.id,
                content: { status: 'ok', history: [[0, 1, 'print(1)'], [0, 2, 'print(2)']] }
            });

            const history = await historyPromise;
            assert.deepStrictEqual(history, [[0, 1, 'print(1)'], [0, 2, 'print(2)']]);
        });
    });

    await t.test('queryKernelHistory() rejects when the kernel replies with an error', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const historyPromise = session.queryKernelHistory();
            await flushMicrotasks();
            const sentFrame = [...ws.sent].map((s) => JSON.parse(s)).reverse().find((f) => f.type === 'history');

            ws.receive({
                type: 'message', channel: 'shell', topic: 'history_reply',
                msg_type: 'history_reply', parent_msg_id: sentFrame.id,
                content: { status: 'error', ename: 'history_request_error', evalue: 'get_range: start is too high' }
            });

            await assert.rejects(historyPromise, /get_range: start is too high/);
        });
    });
});
