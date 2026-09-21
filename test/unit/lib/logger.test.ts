import { test } from 'node:test';
import * as assert from 'node:assert';
import { Logger, defaultLogLevel, passes } from '../../../dist/lib/utils/logger.js';

// Runs `body` with console.log/warn/error captured.
function captured(body: () => void): string[] {
    const lines: string[] = [];
    const saved = { log: console.log, warn: console.warn, error: console.error };
    const grab = (...args: unknown[]) => void lines.push(args.map(String).join(' '));
    console.log = grab;
    console.warn = grab;
    console.error = grab;
    try {
        body();
    } finally {
        Object.assign(console, saved);
    }
    return lines;
}

function everything(logger: Logger): void {
    logger.trace('t');
    logger.debug('d');
    logger.info('i');
    logger.notice('n');
    logger.warn('w');
    logger.error('e');
}

test('defaultLogLevel', async (t) => {
    await t.test("is 'notice' when nothing is set: quiet by default", () => {
        assert.strictEqual(defaultLogLevel({}), 'notice');
    });

    await t.test('takes JOVIAN_LOG_LEVEL, ignoring case and junk', () => {
        assert.strictEqual(defaultLogLevel({ JOVIAN_LOG_LEVEL: 'DEBUG' }), 'debug');
        assert.strictEqual(defaultLogLevel({ JOVIAN_LOG_LEVEL: 'silent' }), 'silent');
        assert.strictEqual(defaultLogLevel({ JOVIAN_LOG_LEVEL: 'loud' }), 'notice');
    });
});

test('passes', () => {
    assert.ok(passes('notice', 'notice'));
    assert.ok(passes('notice', 'error'));
    assert.ok(!passes('notice', 'info'));
    assert.ok(passes('trace', 'trace'));
    assert.ok(!passes('silent', 'error'));
});

test('the console logger', async (t) => {
    await t.test('prints only notices, warnings and errors by default', () => {
        const lines = captured(() => everything(new Logger(undefined, 'notice')));
        assert.deepStrictEqual(lines.map((line) => line.match(/\[(\w+)\]/)?.[1]), ['notice', 'warn', 'error']);
    });

    await t.test('prints everything at trace, nothing when silent', () => {
        assert.strictEqual(captured(() => everything(new Logger(undefined, 'trace'))).length, 6);
        assert.strictEqual(captured(() => everything(new Logger(undefined, 'silent'))).length, 0);
    });

    await t.test('passes data along', () => {
        const lines = captured(() => new Logger(undefined, 'info').info('hello', { a: 1 }));
        assert.match(lines[0] ?? '', /\[info\] hello \[object Object\]/);
    });
});

test('a custom logger receives every message and the console stays silent', () => {
    const received: string[] = [];
    const lines = captured(() => everything(new Logger((level) => void received.push(level), 'silent')));
    assert.deepStrictEqual(received, ['trace', 'debug', 'info', 'notice', 'warn', 'error']);
    assert.strictEqual(lines.length, 0);
});
