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

    lastSentFrame(type: string): any {
        const frame = [...this.sent].reverse()
            .map((s) => JSON.parse(s))
            .find((fr) => fr.type === type);
        assert.ok(frame, `expected a sent frame of type "${type}"`);
        return frame;
    }

    /** Test helper: every sent frame of a given type, oldest first. */
    sentFrames(type: string): any[] {
        return this.sent.map((s) => JSON.parse(s)).filter((fr) => fr.type === type);
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
            ws.receive({ type: 'message', channel: 'iopub', topic: 'status', msg_type: 'status', parent_msg_id: id, content: { execution_state: 'idle' } });

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

            ws.receive({ type: 'message', channel: 'iopub', topic: 'status', msg_type: 'status', parent_msg_id: id, content: { execution_state: 'idle' } });
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
            ws.receive({ type: 'message', channel: 'iopub', topic: 'status', msg_type: 'status', parent_msg_id: id, content: { execution_state: 'idle' } });
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
            ws.receive({ type: 'message', channel: 'iopub', topic: 'status', msg_type: 'status', parent_msg_id: id, content: { execution_state: 'idle' } });
            await resultPromise;

            const history = session.getHistory();
            assert.strictEqual(history.length, 1);
            assert.strictEqual(history[0].code, 'print(42)');
            assert.strictEqual(history[0].executionCount, 1);
            assert.deepStrictEqual(
                history[0].messages.map((m) => m.msgType),
                ['stream', 'execute_reply', 'status']
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

    await t.test('queryKernelHistory() sends a history_request and resolves with the reply', async () => {
        await withFakeWebSocket(async () => {
            const session = new Session(connectionInfo(), {}, fakeSupervisor);
            const ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const historyPromise = session.queryKernelHistory({ n: 5 });
            await flushMicrotasks();

            const sentFrame = ws.lastSentFrame('request');
            assert.strictEqual(sentFrame.msgType, 'history_request');
            assert.strictEqual(sentFrame.channel, 'shell');
            assert.deepStrictEqual(sentFrame.content, { hist_access_type: 'tail', output: false, raw: true, n: 5 });

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
            const sentFrame = ws.lastSentFrame('request');

            ws.receive({
                type: 'message', channel: 'shell', topic: 'history_reply',
                msg_type: 'history_reply', parent_msg_id: sentFrame.id,
                content: { status: 'error', ename: 'history_request_error', evalue: 'get_range: start is too high' }
            });

            await assert.rejects(historyPromise, /get_range: start is too high/);
        });
    });

    // -- Protocol requests (request()/complete()/inspect()/...) -----------

    async function readySession() {
        const session = new Session(connectionInfo(), {}, fakeSupervisor);
        const ws = FakeWebSocket.instances[0];
        ws.receive({ type: 'ready' });
        await session.ready();
        return { session, ws };
    }

    function reply(ws: FakeWebSocket, msgType: string, parentId: string, content: unknown, channel = 'shell') {
        ws.receive({ type: 'message', channel, topic: msgType, msg_type: msgType, parent_msg_id: parentId, content });
    }

    await t.test('complete()/inspect()/isComplete()/kernelInfo()/commInfo() send the right request and resolve with the reply content', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const cases: Array<{ call: () => Promise<any>; msgType: string; content: unknown; replyType: string; replyContent: any }> = [
                {
                    call: () => session.complete('pri', 3),
                    msgType: 'complete_request', content: { code: 'pri', cursor_pos: 3 },
                    replyType: 'complete_reply',
                    replyContent: { status: 'ok', matches: ['print'], cursor_start: 0, cursor_end: 3, metadata: {} }
                },
                {
                    call: () => session.inspect('mean', 4),
                    msgType: 'inspect_request', content: { code: 'mean', cursor_pos: 4, detail_level: 0 },
                    replyType: 'inspect_reply',
                    replyContent: { status: 'ok', found: true, data: { 'text/plain': 'mean(x)' }, metadata: {} }
                },
                {
                    call: () => session.isComplete('1 +'),
                    msgType: 'is_complete_request', content: { code: '1 +' },
                    replyType: 'is_complete_reply',
                    replyContent: { status: 'incomplete', indent: '' }
                },
                {
                    call: () => session.kernelInfo(),
                    msgType: 'kernel_info_request', content: {},
                    replyType: 'kernel_info_reply',
                    replyContent: { status: 'ok', language_info: { name: 'R', version: '4.6.0' } }
                },
                {
                    call: () => session.commInfo('my_target'),
                    msgType: 'comm_info_request', content: { target_name: 'my_target' },
                    replyType: 'comm_info_reply',
                    replyContent: { status: 'ok', comms: {} }
                }
            ];

            for (const c of cases) {
                const promise = c.call();
                await flushMicrotasks();
                const frame = ws.lastSentFrame('request');
                assert.strictEqual(frame.msgType, c.msgType);
                assert.strictEqual(frame.channel, 'shell');
                assert.deepStrictEqual(frame.content, c.content);

                reply(ws, c.replyType, frame.id, c.replyContent);
                assert.deepStrictEqual(await promise, c.replyContent);
            }
        });
    });

    await t.test('request() ignores a reply of the wrong type or for a different request', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const promise = session.kernelInfo();
            await flushMicrotasks();
            const frame = ws.lastSentFrame('request');

            reply(ws, 'kernel_info_reply', 'someone-elses-request', { status: 'ok', language_info: { name: 'WRONG' } });
            reply(ws, 'complete_reply', frame.id, { status: 'ok', matches: [] });
            reply(ws, 'kernel_info_reply', frame.id, { status: 'ok', language_info: { name: 'R' } });

            assert.strictEqual((await promise).language_info.name, 'R');
        });
    });

    await t.test('request() rejects on an error reply, an aborted reply, a supervisor requestError and a timeout', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const errored = session.complete('x');
            await flushMicrotasks();
            reply(ws, 'complete_reply', ws.lastSentFrame('request').id, { status: 'error', ename: 'E', evalue: 'completion blew up' });
            await assert.rejects(errored, /completion blew up/);

            const aborted = session.inspect('x');
            await flushMicrotasks();
            reply(ws, 'inspect_reply', ws.lastSentFrame('request').id, { status: 'aborted' });
            await assert.rejects(aborted, /aborted/);

            const refused = session.kernelInfo();
            await flushMicrotasks();
            ws.receive({ type: 'requestError', id: ws.lastSentFrame('request').id, error: 'session not found' });
            await assert.rejects(refused, /session not found/);

            const timedOut = session.request('kernel_info_request', {}, { timeout: 20 });
            await assert.rejects(timedOut, /Timed out waiting for a kernel_info_reply after 20ms/);
        });
    });

    await t.test('in-flight requests reject when the kernel exits or the session stops', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            session.on('exit', () => {});

            const first = session.kernelInfo();
            await flushMicrotasks();
            ws.receive({ type: 'kernelExit', reason: 'boom' });
            await assert.rejects(first, /Session process exited: boom/);

            const second = session.kernelInfo();
            await flushMicrotasks();
            const secondRejected = assert.rejects(second, /Session stopped/);
            await session.stop();
            await secondRejected;
        });
    });

    await t.test('interrupt() sends interrupt_request on the control channel and resolves true on an ok interrupt_reply', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const promise = session.interrupt();
            await flushMicrotasks();
            const frame = ws.lastSentFrame('request');
            assert.strictEqual(frame.msgType, 'interrupt_request');
            assert.strictEqual(frame.channel, 'control');

            reply(ws, 'interrupt_reply', frame.id, { status: 'ok' }, 'control');
            assert.strictEqual(await promise, true);
        });
    });

    await t.test('interrupt() resolves false (never rejects) when the kernel does not answer in time', async () => {
        await withFakeWebSocket(async () => {
            const { session } = await readySession();
            assert.strictEqual(await session.interrupt({ timeout: 20 }), false);
        });
    });

    await t.test('comm methods send comm_open/comm_msg/comm_close on shell and return the ids they used', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const opened = await session.commOpen('my_target', { hello: 1 }, 'comm-1');
            assert.strictEqual(opened.commId, 'comm-1');
            const msgId = await session.commMsg('comm-1', { x: 2 });
            const closeId = await session.commClose('comm-1');

            const frames = ws.sentFrames('request');
            assert.deepStrictEqual(frames.map((f) => f.msgType), ['comm_open', 'comm_msg', 'comm_close']);
            assert.ok(frames.every((f) => f.channel === 'shell'));
            assert.deepStrictEqual(frames[0].content, { comm_id: 'comm-1', target_name: 'my_target', data: { hello: 1 } });
            assert.deepStrictEqual(frames[1].content, { comm_id: 'comm-1', data: { x: 2 } });
            assert.deepStrictEqual(frames[2].content, { comm_id: 'comm-1', data: {} });
            assert.deepStrictEqual(frames.map((f) => f.id), [opened.msgId, msgId, closeId]);
        });
    });

    await t.test('a requestError for a fire-and-forget comm is surfaced as a "requestError" event', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            const errors: Array<{ id: string; error: Error }> = [];
            session.on('requestError', (e) => errors.push(e));

            const { msgId } = await session.commOpen('t');
            ws.receive({ type: 'requestError', id: msgId, error: 'session not found' });

            assert.strictEqual(errors.length, 1);
            assert.strictEqual(errors[0].id, msgId);
            assert.match(errors[0].error.message, /session not found/);
        });
    });

    await t.test('kernel comm messages arrive as comm_open/comm_msg/comm_close events', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            const seen: string[] = [];
            for (const type of ['comm_open', 'comm_msg', 'comm_close']) {
                session.on(type, (content) => seen.push(`${type}:${content.comm_id}`));
            }

            reply(ws, 'comm_open', 'p', { comm_id: 'c1', target_name: 't', data: {} }, 'iopub');
            reply(ws, 'comm_msg', 'p', { comm_id: 'c1', data: { a: 1 } }, 'iopub');
            reply(ws, 'comm_close', 'p', { comm_id: 'c1', data: {} }, 'iopub');
            await flushMicrotasks();

            assert.deepStrictEqual(seen, ['comm_open:c1', 'comm_msg:c1', 'comm_close:c1']);
        });
    });

    await t.test('executionState follows the kernel\'s iopub status messages', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            assert.strictEqual(session.executionState, undefined);

            reply(ws, 'status', 'p', { execution_state: 'busy' }, 'iopub');
            await flushMicrotasks();
            assert.strictEqual(session.executionState, 'busy');
            reply(ws, 'status', 'p', { execution_state: 'idle' }, 'iopub');
            await flushMicrotasks();
            assert.strictEqual(session.executionState, 'idle');
        });
    });

    await t.test('stop() waits for the kernel\'s shutdown_reply so it is observable before "stopped"', async () => {
        await withFakeWebSocket(async () => {
            let ws!: FakeWebSocket;
            // The reply lands while the supervisor's stop call is still in flight.
            const supervisor: any = {
                stopSession: async () => {
                    reply(ws, 'shutdown_reply', 'stop-req', { status: 'ok', restart: false }, 'control');
                }
            };
            const session = new Session(connectionInfo(), {}, supervisor);
            ws = FakeWebSocket.instances[0];
            ws.receive({ type: 'ready' });
            await session.ready();

            const order: string[] = [];
            session.on('shutdown_reply', (content) => order.push(`shutdown_reply:restart=${content.restart}`));
            session.on('stopped', () => order.push('stopped'));

            await session.stop();
            assert.deepStrictEqual(order, ['shutdown_reply:restart=false', 'stopped']);
        });
    });

    // -- execute(): user_expressions / stop_on_error / display updates ----

    await t.test('execute() forwards userExpressions/stopOnError and resolves with the reply\'s user_expressions', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const resultPromise = session.execute('x <- 21', { userExpressions: { double: 'x * 2' }, stopOnError: true });
            await flushMicrotasks();
            const frame = ws.lastSentFrame('execute');
            assert.deepStrictEqual(frame.options.userExpressions, { double: 'x * 2' });
            assert.strictEqual(frame.options.stopOnError, true);

            const userExpressions = { double: { status: 'ok', data: { 'text/plain': '[1] 42' }, metadata: {} } };
            reply(ws, 'execute_reply', frame.id, { status: 'ok', execution_count: 1, user_expressions: userExpressions });
            reply(ws, 'status', frame.id, { execution_state: 'idle' });

            const result = await resultPromise;
            assert.strictEqual(result.success, true);
            assert.deepStrictEqual(result.userExpressions, userExpressions);
        });
    });

    await t.test('a failure with stopOnError aborts the executions queued behind it, unrun', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const first = session.execute('stop("no")', { stopOnError: true });
            const second = session.execute('1 + 1');
            const third = session.execute('2 + 2');
            await flushMicrotasks();

            const firstId = ws.lastSentId('execute');
            ws.receive({
                type: 'message', channel: 'iopub', topic: 'error', msg_type: 'error', parent_msg_id: firstId,
                content: { ename: 'simpleError', evalue: 'no', traceback: [] }
            });
            session.on('error', () => {});

            const [r1, r2, r3] = await Promise.all([first.catch((e) => e), second, third]);
            assert.strictEqual((r1 as any).success, false);
            for (const aborted of [r2, r3]) {
                assert.strictEqual(aborted.success, false);
                assert.strictEqual(aborted.aborted, true);
                assert.strictEqual(aborted.status, 'aborted');
                assert.deepStrictEqual(aborted.output, []);
            }
            assert.strictEqual(ws.sentFrames('execute').length, 1, 'the aborted executions must never be sent to the kernel');
        });
    });

    await t.test('without stopOnError a failure does not abort what is queued behind it', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            session.on('error', () => {});

            const first = session.execute('stop("no")');
            const second = session.execute('1 + 1');
            await flushMicrotasks();

            ws.receive({
                type: 'message', channel: 'iopub', topic: 'error', msg_type: 'error', parent_msg_id: ws.lastSentId('execute'),
                content: { ename: 'simpleError', evalue: 'no', traceback: [] }
            });
            await first;
            await flushMicrotasks();

            const secondId = ws.lastSentId('execute');
            reply(ws, 'execute_reply', secondId, { status: 'ok', execution_count: 2 });
            reply(ws, 'status', secondId, { execution_state: 'idle' });
            assert.strictEqual((await second).success, true);
            assert.strictEqual(ws.sentFrames('execute').length, 2);
        });
    });

    await t.test('a kernel-aborted execute_reply resolves as aborted', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const promise = session.execute('1 + 1');
            await flushMicrotasks();
            reply(ws, 'execute_reply', ws.lastSentId('execute'), { status: 'aborted' });

            const result = await promise;
            assert.strictEqual(result.success, false);
            assert.strictEqual(result.aborted, true);
            assert.match(result.error!.message, /aborted/);
        });
    });

    await t.test('update_display_data and clear_output are collected into the execution\'s output', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const promise = session.execute('display()');
            await flushMicrotasks();
            const id = ws.lastSentId('execute');

            reply(ws, 'display_data', id, { data: { 'text/plain': 'a' }, metadata: {}, transient: { display_id: 'd1' } }, 'iopub');
            reply(ws, 'update_display_data', id, { data: { 'text/plain': 'b' }, metadata: {}, transient: { display_id: 'd1' } }, 'iopub');
            reply(ws, 'clear_output', id, { wait: true }, 'iopub');
            reply(ws, 'execute_reply', id, { status: 'ok', execution_count: 1 });
            reply(ws, 'status', id, { execution_state: 'idle' });

            const result = await promise;
            assert.deepStrictEqual(result.output.map((m) => m.msgType), ['display_data', 'update_display_data', 'clear_output']);
        });
    });

    // -- stderr, Comm objects, execution state, restart options ----------

    await t.test('stderr stream messages are emitted as "stderr", not "stdout"', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            const out: string[] = [];
            const err: string[] = [];
            session.on('stdout', (text: string) => out.push(text));
            session.on('stderr', (text: string) => err.push(text));

            reply(ws, 'stream', 'p', { name: 'stdout', text: 'to stdout' }, 'iopub');
            reply(ws, 'stream', 'p', { name: 'stderr', text: 'to stderr' }, 'iopub');
            await flushMicrotasks();

            assert.deepStrictEqual(out, ['to stdout']);
            assert.deepStrictEqual(err, ['to stderr']);
        });
    });

    await t.test('executionState stays busy while an execution runs even if an interrupt (handled meanwhile) goes idle', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            reply(ws, 'status', 'exec-1', { execution_state: 'busy' }, 'iopub');
            reply(ws, 'status', 'int-1', { execution_state: 'busy' }, 'iopub');
            reply(ws, 'status', 'int-1', { execution_state: 'idle' }, 'iopub');
            await flushMicrotasks();
            assert.strictEqual(session.executionState, 'busy');

            reply(ws, 'status', 'exec-1', { execution_state: 'idle' }, 'iopub');
            await flushMicrotasks();
            assert.strictEqual(session.executionState, 'idle');
        });
    });

    await t.test('a kernel-initiated comm arrives as a "comm" event with a working Comm object', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const opened: Array<{ comm: any; data: any }> = [];
            session.on('comm', (comm, data) => opened.push({ comm, data }));

            reply(ws, 'comm_open', 'p', { comm_id: 'k1', target_name: 'from_kernel', data: { hi: 1 } }, 'iopub');
            await flushMicrotasks();
            assert.strictEqual(opened.length, 1);
            const { comm, data } = opened[0];
            assert.strictEqual(comm.id, 'k1');
            assert.strictEqual(comm.targetName, 'from_kernel');
            assert.deepStrictEqual(data, { hi: 1 });

            const messages: unknown[] = [];
            const closes: unknown[] = [];
            comm.on('message', (d: unknown) => messages.push(d));
            comm.on('close', (d: unknown) => closes.push(d));

            reply(ws, 'comm_msg', 'p', { comm_id: 'k1', data: { n: 1 } }, 'iopub');
            reply(ws, 'comm_msg', 'other-comm', { comm_id: 'unknown', data: { n: 99 } }, 'iopub');
            await flushMicrotasks();
            assert.deepStrictEqual(messages, [{ n: 1 }]);

            await comm.send({ back: true });
            const sent = ws.lastSentFrame('request');
            assert.strictEqual(sent.msgType, 'comm_msg');
            assert.deepStrictEqual(sent.content, { comm_id: 'k1', data: { back: true } });

            reply(ws, 'comm_close', 'p', { comm_id: 'k1', data: { bye: 1 } }, 'iopub');
            await flushMicrotasks();
            assert.deepStrictEqual(closes, [{ bye: 1 }]);
            assert.strictEqual(comm.closed, true);
            await assert.rejects(comm.send({}), /closed/);
        });
    });

    await t.test('openComm() returns a Comm registered before the request goes out, and an unknown target closes it', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const comm = await session.openComm('no_such_target', { x: 1 });
            const frame = ws.lastSentFrame('request');
            assert.strictEqual(frame.msgType, 'comm_open');
            assert.strictEqual(frame.content.comm_id, comm.id);
            assert.strictEqual(frame.content.target_name, 'no_such_target');

            const closed = new Promise((resolve) => comm.once('close', resolve));
            reply(ws, 'comm_close', frame.id, { comm_id: comm.id, data: {} }, 'iopub');
            await closed;
            assert.strictEqual(comm.closed, true);
        });
    });

    await t.test('comms are closed (with a reason) when the session stops', async () => {
        await withFakeWebSocket(async () => {
            const { session } = await readySession();
            const comm = await session.openComm('t');
            const closed = new Promise<any>((resolve) => comm.once('close', resolve));

            await session.stop();
            assert.deepStrictEqual(await closed, { reason: 'session stopped' });
        });
    });

    await t.test('restart(options) sends the merge of the current options and the new ones, and keeps it', async () => {
        await withFakeWebSocket(async () => {
            const restartCalls: unknown[] = [];
            const supervisor: any = {
                stopSession: async () => {},
                restartSession: async (_info: unknown, options: unknown) => { restartCalls.push(options); }
            };
            const session = new Session(
                connectionInfo(),
                { rHome: '/opt/R-4.4', workingDirectory: '/projects/a', rLibs: '/libs' },
                supervisor
            );
            const first = FakeWebSocket.instances[0];
            first.receive({ type: 'ready' });
            await session.ready();

            const restarting = session.restart({ rHome: '/opt/R-4.6' });
            await flushMicrotasks();
            // restart() reconnects: the second socket has to say it is ready.
            await new Promise((resolve) => setTimeout(resolve, 260));
            FakeWebSocket.instances[FakeWebSocket.instances.length - 1].receive({ type: 'ready' });
            await restarting;

            assert.deepStrictEqual(restartCalls, [{ rHome: '/opt/R-4.6', workingDirectory: '/projects/a', rLibs: '/libs' }]);
            assert.strictEqual(session.options.rHome, '/opt/R-4.6');
            assert.strictEqual(session.options.workingDirectory, '/projects/a');
        });
    });

    await t.test('a timed-out execute() interrupts the kernel by default', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            const running = session.execute('Sys.sleep(60)', { timeout: 20 });
            await assert.rejects(running, /Execution timed out after 20ms \(the kernel was interrupted\)/);
            await flushMicrotasks();

            const frame = ws.lastSentFrame('request');
            assert.strictEqual(frame.msgType, 'interrupt_request');
            assert.strictEqual(frame.channel, 'control');
            reply(ws, 'interrupt_reply', frame.id, { status: 'ok' }, 'control');
        });
    });

    await t.test('interruptOnTimeout: false leaves the kernel alone after a timeout', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            await assert.rejects(
                session.execute('Sys.sleep(60)', { timeout: 20, interruptOnTimeout: false }),
                (error: Error) => /Execution timed out after 20ms$/.test(error.message)
            );
            await flushMicrotasks();

            assert.strictEqual(ws.sentFrames('request').length, 0);
        });
    });

    await t.test('getHistory() bounds the stream text kept per execution to the newest, and flags it', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();

            reply(ws, 'execute_input', 'exec-1', { code: 'flood', execution_count: 1 }, 'iopub');
            reply(ws, 'execute_result', 'exec-1', { execution_count: 1, data: { 'text/plain': 'kept' }, metadata: {} }, 'iopub');
            const chunk = 'x'.repeat(10000) + '\n';
            for (let i = 0; i < 200; i++) {
                reply(ws, 'stream', 'exec-1', { name: 'stdout', text: chunk + i }, 'iopub');
            }
            await flushMicrotasks();

            const [entry] = session.getHistory();
            const streams = entry.messages.filter((m) => m.msgType === 'stream');
            const chars = streams.reduce((n, m) => n + String(m.content.text).length, 0);

            assert.ok(chars <= 500_000 + 10_100, `kept ${chars} chars of stream text`);
            assert.strictEqual(entry.truncated, true);
            // The newest output survives, and non-stream messages are untouched.
            assert.ok(String(streams.at(-1)!.content.text).endsWith('199'));
            assert.ok(entry.messages.some((m) => m.msgType === 'execute_result'));
        });
    });

    await t.test('getHistory() does not flag an execution with modest output', async () => {
        await withFakeWebSocket(async () => {
            const { session, ws } = await readySession();
            reply(ws, 'execute_input', 'exec-1', { code: 'x', execution_count: 1 }, 'iopub');
            reply(ws, 'stream', 'exec-1', { name: 'stdout', text: 'hello' }, 'iopub');
            await flushMicrotasks();
            assert.strictEqual(session.getHistory()[0].truncated, undefined);
        });
    });
});
