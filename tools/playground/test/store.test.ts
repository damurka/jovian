import { test } from 'node:test';
import * as assert from 'node:assert';
import { initialState, newSessionView, reducer, type State } from '../lib/client/store.ts';
import type { WireMessage } from '../lib/types.ts';

const msg = (msgType: string, content: unknown): WireMessage => ({
    topic: msgType, msgType, channel: 'iopub', parentMsgId: 'p', content
});

function withSession(): State {
    return reducer(initialState, {
        type: 'add',
        session: newSessionView({ id: 's1', name: 'S', kernelType: 'r', status: 'ready', config: {} })
    });
}

test('a run attaches live output to its cell and reconciles the count with the kernel', () => {
    let s = withSession();
    s = reducer(s, { type: 'runStarted', id: 's1', key: 'k1', code: '1+1', time: 0 });
    assert.strictEqual(s.sessions.s1.running, true);
    assert.strictEqual(s.sessions.s1.transcript.cells[0].inNum, 1);

    s = reducer(s, { type: 'message', id: 's1', message: msg('execute_result', { data: { 'text/plain': '[1] 2' } }) });
    // The kernel says it is really on execution 7 (this page just reconnected).
    s = reducer(s, { type: 'runFinished', id: 's1', key: 'k1', executionCount: 7 });

    const view = s.sessions.s1;
    assert.strictEqual(view.running, false);
    assert.strictEqual(view.execCount, 7);
    assert.strictEqual(view.transcript.cells[0].inNum, 7);
    assert.strictEqual(view.transcript.cells[0].outputs.length, 1);
    assert.strictEqual(view.transcript.currentKey, null);
});

test('a failed run leaves a system note on its cell', () => {
    let s = withSession();
    s = reducer(s, { type: 'runStarted', id: 's1', key: 'k1', code: 'x', time: 0 });
    s = reducer(s, { type: 'runFinished', id: 's1', key: 'k1', failure: 'Execution timed out after 3000ms' });
    const out = s.sessions.s1.transcript.cells[0].outputs[0];
    assert.ok(out.kind === 'text' && out.cls === 'system' && out.text.includes('timed out'));
});

test('hydrate replays recorded executions through the same reducer and keeps the kernel numbering', () => {
    let s = withSession();
    s = reducer(s, {
        type: 'hydrate', id: 's1',
        history: [
            { code: 'print(1)', executionCount: 5, time: 1, messages: [msg('stream', { name: 'stdout', text: '1\n' })] },
            { code: 'stop("x")', executionCount: 6, time: 2, messages: [msg('error', { ename: 'e', evalue: 'x', traceback: [] })] }
        ]
    });
    const view = s.sessions.s1;
    assert.deepStrictEqual(view.transcript.cells.map((c) => [c.inNum, c.code, c.outputs.length]), [[5, 'print(1)', 1], [6, 'stop("x")', 1]]);
    assert.strictEqual(view.execCount, 6);
    assert.strictEqual(view.transcript.currentKey, null);
});

test('hydrate never overwrites a transcript that already has cells', () => {
    let s = withSession();
    s = reducer(s, { type: 'runStarted', id: 's1', key: 'k1', code: 'live', time: 0 });
    s = reducer(s, { type: 'hydrate', id: 's1', history: [{ code: 'old', executionCount: 1, time: 0, messages: [] }] });
    assert.deepStrictEqual(s.sessions.s1.transcript.cells.map((c) => c.code), ['live']);
});

test('a notice with no running cell lands on the most recent one instead of vanishing', () => {
    let s = withSession();
    s = reducer(s, { type: 'runStarted', id: 's1', key: 'k1', code: 'x', time: 0 });
    s = reducer(s, { type: 'runFinished', id: 's1', key: 'k1' });
    s = reducer(s, { type: 'notice', id: 's1', cls: 'error', text: 'Restart failed' });
    assert.strictEqual(s.sessions.s1.transcript.cells[0].outputs.length, 1);
    assert.strictEqual(s.sessions.s1.transcript.currentKey, null);
});

test('removing the active session clears the selection', () => {
    let s = withSession();
    s = reducer(s, { type: 'select', id: 's1' });
    s = reducer(s, { type: 'remove', id: 's1' });
    assert.strictEqual(s.activeId, null);
    assert.deepStrictEqual(s.order, []);
});

test('an execution that ends with an empty failure (an interrupt) is labelled as interrupted', () => {
    let s = reducer(initialState, { type: 'add', session: newSessionView({ id: 's1', name: 'S', kernelType: 'r', status: 'ready', config: {} }) });
    s = reducer(s, { type: 'runStarted', id: 's1', key: 'k1', code: 'Sys.sleep(60)', time: 0 });
    s = reducer(s, { type: 'runFinished', id: 's1', key: 'k1', failure: '' });

    const last = s.sessions.s1.transcript.cells[0].outputs.at(-1);
    assert.ok(last && last.kind === 'text');
    assert.strictEqual(last.text, 'Execution interrupted');
});
