import { test } from 'node:test';
import * as assert from 'node:assert';
import { createFrameBatcher, type Scheduler } from '../lib/client/frame-batcher.ts';

// A scheduler the test drives by hand.
function manual() {
    let next = 0;
    const frames = new Map<number, () => void>();
    const timers = new Map<number, () => void>();
    const scheduler: Scheduler = {
        requestFrame: (cb) => { frames.set(++next, cb); return next; },
        cancelFrame: (h) => { frames.delete(h as number); },
        setTimer: (cb) => { timers.set(++next, cb); return next; },
        clearTimer: (h) => { timers.delete(h as number); }
    };
    const run = (m: Map<number, () => void>) => { for (const cb of [...m.values()]) cb(); };
    return { scheduler, frames, timers, frame: () => run(frames), timeout: () => run(timers) };
}

test('items pushed within a frame arrive together, in order, once', () => {
    const clock = manual();
    const batches: number[][] = [];
    const b = createFrameBatcher<number>((items) => batches.push(items), { scheduler: clock.scheduler });
    for (let i = 0; i < 1000; i++) b.push(i);
    assert.strictEqual(clock.frames.size, 1, 'one frame requested, not one per item');
    assert.deepStrictEqual(batches, []);
    clock.frame();
    assert.strictEqual(batches.length, 1);
    assert.deepStrictEqual(batches[0], Array.from({ length: 1000 }, (_, i) => i));
    assert.strictEqual(clock.timers.size, 0, 'the fallback timer is cancelled');
    b.push(1000);
    clock.frame();
    assert.deepStrictEqual(batches[1], [1000]);
});

test('the timer delivers when frames do not run (a background tab)', () => {
    const clock = manual();
    const batches: string[][] = [];
    const b = createFrameBatcher<string>((items) => batches.push(items), { scheduler: clock.scheduler });
    b.push('a'); b.push('b');
    clock.timeout();
    assert.deepStrictEqual(batches, [['a', 'b']]);
    assert.strictEqual(clock.frames.size, 0);
});

test('flush delivers now; dispose drops what waits', () => {
    const clock = manual();
    const batches: string[][] = [];
    const b = createFrameBatcher<string>((items) => batches.push(items), { scheduler: clock.scheduler });
    b.flush();
    assert.deepStrictEqual(batches, [], 'nothing waiting, nothing delivered');
    b.push('a');
    b.flush();
    assert.deepStrictEqual(batches, [['a']]);
    b.push('b');
    b.dispose();
    clock.frame(); clock.timeout();
    assert.deepStrictEqual(batches, [['a']]);
});
