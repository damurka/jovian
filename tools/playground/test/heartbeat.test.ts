import { test } from 'node:test';
import * as assert from 'node:assert';
import { describeHeartbeat } from '../lib/client/heartbeat.ts';

test('a fast answered ping is ok, with sub-10ms shown to one decimal', () => {
    assert.deepStrictEqual(describeHeartbeat({ rttMs: 0.8, sinceLastPongMs: 120, misses: 0 }, 'ready'), { text: '0.8ms', tone: 'ok' });
    assert.deepStrictEqual(describeHeartbeat({ rttMs: 12.4, sinceLastPongMs: 50, misses: 0 }, 'ready'), { text: '12ms', tone: 'ok' });
});

test('a slow round trip warns, a very slow one is bad', () => {
    assert.strictEqual(describeHeartbeat({ rttMs: 400, sinceLastPongMs: 10, misses: 0 }, 'ready').tone, 'warn');
    assert.strictEqual(describeHeartbeat({ rttMs: 1500, sinceLastPongMs: 10, misses: 0 }, 'ready').tone, 'bad');
});

test('missed pings or a stale pong mean no reply', () => {
    assert.deepStrictEqual(describeHeartbeat({ rttMs: 5, sinceLastPongMs: 100, misses: 2 }, 'ready'), { text: 'no reply', tone: 'bad' });
    assert.deepStrictEqual(describeHeartbeat({ rttMs: 5, sinceLastPongMs: 9000, misses: 0 }, 'ready'), { text: 'no reply', tone: 'bad' });
});

test('nothing measured yet, a stopped or starting session is idle; a crashed one is lost', () => {
    assert.strictEqual(describeHeartbeat(null, 'ready').tone, 'idle');
    assert.strictEqual(describeHeartbeat({ rttMs: null, sinceLastPongMs: null, misses: 0 }, 'ready').tone, 'idle');
    assert.strictEqual(describeHeartbeat({ rttMs: 3, sinceLastPongMs: 1, misses: 0 }, 'stopped').tone, 'idle');
    assert.strictEqual(describeHeartbeat({ rttMs: 3, sinceLastPongMs: 1, misses: 0 }, 'starting').tone, 'idle');
    assert.deepStrictEqual(describeHeartbeat({ rttMs: 3, sinceLastPongMs: 1, misses: 0 }, 'crashed'), { text: 'lost', tone: 'bad' });
});
