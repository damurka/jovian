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
});
