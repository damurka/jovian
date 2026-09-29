import { test } from 'node:test';
import * as assert from 'node:assert';
import {
    MAX_OUTPUT_CHARS, answerInput, applyMessage, applyMessages, emptyTranscript, formatMimeData, hasPendingInput, startCell
} from '../lib/transcript.ts';
import type { WireMessage } from '../lib/types.ts';

const msg = (msgType: string, content: unknown): WireMessage => ({
    topic: msgType, msgType, channel: 'iopub', parentMsgId: 'p', content
});

function withCell() {
    return startCell(emptyTranscript(), { key: 'c1', inNum: 1, code: 'x', time: 0 });
}

test('formatMimeData prefers an image, then text/plain, then JSON', () => {
    assert.deepStrictEqual(formatMimeData({ 'image/png': 'AAAA', 'text/plain': 'x' }), { kind: 'image', src: 'data:image/png;base64,AAAA' });
    assert.deepStrictEqual(formatMimeData({ 'image/png': 'data:image/png;base64,BB' }), { kind: 'image', src: 'data:image/png;base64,BB' });
    assert.deepStrictEqual(formatMimeData({ 'text/plain': ['a', 'b'] }), { kind: 'text', text: 'a\nb' });
    assert.deepStrictEqual(formatMimeData({ 'text/html': '<b>x</b>' }), { kind: 'text', text: '{"text/html":"<b>x</b>"}' });
    assert.strictEqual(formatMimeData(undefined), null);
});

test('stream messages become stdout/stderr text outputs on the current cell', () => {
    let t = withCell();
    t = applyMessage(t, msg('stream', { name: 'stdout', text: 'hi\n' }));
    t = applyMessage(t, msg('stream', { name: 'stderr', text: 'oops\n' }));
    assert.deepStrictEqual(t.cells[0].outputs.map((o) => o.kind === 'text' && [o.cls, o.text]), [['', 'hi\n'], ['stderr', 'oops\n']]);
});

test('adjacent chunks of the same stream merge into one output; a different stream or output starts a new one', () => {
    let t = withCell();
    for (const text of ['Hello,', ' ', 'David', '\n']) t = applyMessage(t, msg('stream', { name: 'stdout', text }));
    t = applyMessage(t, msg('stream', { name: 'stderr', text: 'warn' }));
    t = applyMessage(t, msg('stream', { name: 'stdout', text: 'after' }));
    assert.deepStrictEqual(
        t.cells[0].outputs.map((o) => o.kind === 'text' && [o.cls, o.text]),
        [['', 'Hello, David\n'], ['stderr', 'warn'], ['', 'after']]
    );
});

test('messages with no current cell are dropped', () => {
    const t = applyMessage(emptyTranscript(), msg('stream', { name: 'stdout', text: 'lost' }));
    assert.strictEqual(t.cells.length, 0);
});

test('errors render as "ename: evalue" plus the traceback', () => {
    const t = applyMessage(withCell(), msg('error', { ename: 'ValueError', evalue: 'bad', traceback: ['line 1', 'line 2'] }));
    const out = t.cells[0].outputs[0];
    assert.ok(out.kind === 'text' && out.cls === 'error');
    assert.strictEqual(out.text, 'ValueError: bad\nline 1\nline 2');
});

test('clear_output empties the cell now, or on the next output with wait=true', () => {
    let t = applyMessage(withCell(), msg('stream', { name: 'stdout', text: 'a' }));
    t = applyMessage(t, msg('clear_output', { wait: false }));
    assert.strictEqual(t.cells[0].outputs.length, 0);

    t = applyMessage(t, msg('stream', { name: 'stdout', text: 'b' }));
    t = applyMessage(t, msg('clear_output', { wait: true }));
    assert.strictEqual(t.cells[0].outputs.length, 1, 'wait=true keeps the old output until something new arrives');
    t = applyMessage(t, msg('stream', { name: 'stdout', text: 'c' }));
    assert.deepStrictEqual(t.cells[0].outputs.map((o) => o.kind === 'text' && o.text), ['c']);
});

test('update_display_data replaces the display with the same id, even in an earlier cell', () => {
    let t = withCell();
    t = applyMessage(t, msg('display_data', { data: { 'text/plain': 'first' }, transient: { display_id: 'd1' } }));
    t = startCell(t, { key: 'c2', inNum: 2, code: 'y', time: 1 });
    t = applyMessage(t, msg('update_display_data', { data: { 'text/plain': 'second' }, transient: { display_id: 'd1' } }));

    assert.strictEqual(t.cells[1].outputs.length, 0, 'updated in place, not appended to the running cell');
    const updated = t.cells[0].outputs[0];
    assert.ok(updated.kind === 'text');
    assert.strictEqual(updated.text, 'second');
});

test('update_display_data for an unknown display id appends like display_data', () => {
    const t = applyMessage(withCell(), msg('update_display_data', { data: { 'text/plain': 'x' }, transient: { display_id: 'nope' } }));
    assert.strictEqual(t.cells[0].outputs.length, 1);
});

test('an image update turns a text display into an image', () => {
    let t = applyMessage(withCell(), msg('display_data', { data: { 'text/plain': 'loading' }, transient: { display_id: 'p' } }));
    t = applyMessage(t, msg('update_display_data', { data: { 'image/png': 'AAAA' }, transient: { display_id: 'p' } }));
    const out = t.cells[0].outputs[0];
    assert.ok(out.kind === 'image');
    assert.strictEqual(out.src, 'data:image/png;base64,AAAA');
});

test('input_request adds a pending prompt that answerInput() resolves', () => {
    let t = applyMessage(withCell(), msg('input_request', { prompt: 'name? ', password: false }));
    assert.strictEqual(hasPendingInput(t), true);
    t = answerInput(t, 'World');
    assert.strictEqual(hasPendingInput(t), false);
    const out = t.cells[0].outputs[0];
    assert.ok(out.kind === 'input' && out.answered && out.answeredValue === 'World');
});

test('status, execute_input and other messages leave the transcript alone', () => {
    const t = withCell();
    assert.strictEqual(applyMessage(t, msg('status', { execution_state: 'busy' })), t);
    assert.strictEqual(applyMessage(t, msg('execute_input', { code: 'x', execution_count: 1 })), t);
});

test('a flood of stream text keeps only the tail of the block and says so', () => {
    let t = withCell();
    const line = '[1] 12345678\n';
    for (let i = 0; i < 40000; i++) t = applyMessage(t, msg('stream', { name: 'stdout', text: line.repeat(10) }));

    const out = t.cells[0].outputs;
    assert.strictEqual(out.length, 1);
    assert.strictEqual(out[0].kind, 'text');
    const block = out[0] as { text: string; truncated?: boolean };
    assert.ok(block.text.length <= MAX_OUTPUT_CHARS);
    assert.strictEqual(block.truncated, true);
    assert.ok(block.text.startsWith('[1] '), 'starts on a line boundary');
    assert.ok(block.text.endsWith('\n'));
});

test('short output is never marked truncated', () => {
    const t = applyMessage(withCell(), msg('stream', { name: 'stdout', text: 'small\n' }));
    assert.ok(!(t.cells[0].outputs[0] as { truncated?: boolean }).truncated);
});

test('applyMessages gives the transcript applyMessage gives one message at a time', () => {
    const stream = (name: string, text: string): WireMessage =>
        ({ topic: 'stream', msgType: 'stream', channel: 'iopub', parentMsgId: 'p', content: { name, text } });
    const other = (msgType: string, content: unknown): WireMessage =>
        ({ topic: msgType, msgType, channel: 'iopub', parentMsgId: 'p', content });
    const messages: WireMessage[] = [
        stream('stdout', 'a'), stream('stdout', 'b\n'), stream('stderr', 'w1\n'), stream('stdout', 'c\n'),
        other('display_data', { data: { 'text/plain': 'shown' }, transient: { display_id: 'd' } }),
        stream('stdout', 'after display\n'),
        other('clear_output', { wait: true }), stream('stderr', 'fresh\n'), stream('stderr', 'more\n'),
        other('update_display_data', { data: { 'text/plain': 'updated' }, transient: { display_id: 'd' } }),
        other('status', { execution_state: 'idle' })
    ];
    for (const prefix of [[], [stream('stdout', 'earlier\n')]]) {
        let start = startCell(emptyTranscript(), { key: 'k', inNum: 1, code: 'x', time: 0 });
        start = applyMessages(start, prefix);
        let one = start;
        for (const m of messages) one = applyMessage(one, m);
        assert.deepStrictEqual(applyMessages(start, messages), one);
    }
});

test('applyMessages takes an interleaved stdout/stderr flood in linear time', () => {
    const N = 50_000;
    const messages: WireMessage[] = Array.from({ length: N }, (_, i) => ({
        topic: 'stream', msgType: 'stream', channel: 'iopub', parentMsgId: 'p',
        content: { name: i % 2 ? 'stderr' : 'stdout', text: `line ${i}\n` }
    }));
    const t0 = performance.now();
    const t = applyMessages(startCell(emptyTranscript(), { key: 'k', inNum: 1, code: 'x', time: 0 }), messages);
    assert.ok(performance.now() - t0 < 2000, 'batched flood is not quadratic');
    assert.strictEqual(t.cells[0].outputs.length, N);
    assert.strictEqual(t.nextOutputId, N + 1);
});

test('applyMessages caps a one-stream flood and marks it truncated', () => {
    const messages: WireMessage[] = Array.from({ length: 40_000 }, (_, i) => ({
        topic: 'stream', msgType: 'stream', channel: 'iopub', parentMsgId: 'p', content: { name: 'stdout', text: `line ${i}\n` }
    }));
    const t = applyMessages(startCell(emptyTranscript(), { key: 'k', inNum: 1, code: 'x', time: 0 }), messages);
    const out = t.cells[0].outputs;
    assert.strictEqual(out.length, 1);
    assert.ok(out[0].kind === 'text' && out[0].truncated && out[0].text.length <= MAX_OUTPUT_CHARS);
    assert.ok(out[0].kind === 'text' && out[0].text.endsWith('line 39999\n') && out[0].text.startsWith('line '));
});
