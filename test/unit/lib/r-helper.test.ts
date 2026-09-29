import { test } from 'node:test';
import * as assert from 'node:assert';
import {
    RHelper,
    attachCode,
    mergeCompletions,
    parseRState,
    type HelperSession
} from '../../../dist/lib/session/r-helper.js';

test('parseRState', async (t) => {
    await t.test('reads the attached packages and global names from the state expression', () => {
        const state = parseRState({
            status: 'ok',
            metadata: {},
            data: { 'text/plain': '{"search":[".GlobalEnv","package:purrr","package:stats","Autoloads","package:base"],"globals":["my_data"]} ' }
        });
        assert.deepStrictEqual(state, { packages: ['purrr', 'stats', 'base'], globals: ['my_data'] });
    });

    await t.test('is undefined for a failed or unreadable result', () => {
        assert.strictEqual(parseRState(undefined), undefined);
        assert.strictEqual(parseRState({ status: 'error', ename: 'Error', evalue: 'no jsonlite', traceback: [] }), undefined);
        assert.strictEqual(parseRState({ status: 'ok', metadata: {}, data: { 'text/plain': 'not json' } }), undefined);
    });
});

test('attachCode attaches every package quietly and leaves no variable behind', () => {
    const code = attachCode(['purrr', 'my"pkg']);
    assert.ok(code.includes('c("purrr", "my\\"pkg")'), code);
    assert.ok(code.includes('suppressPackageStartupMessages(library(.jovian_p, character.only = TRUE))'));
    assert.ok(code.includes('try('));
    assert.ok(code.endsWith('rm(.jovian_p)'));
});

test('mergeCompletions', async (t) => {
    await t.test('adds the busy session’s own names that match what is typed', () => {
        const merged = mergeCompletions({ matches: ['walk', 'walk2'], cursor_start: 0, cursor_end: 3 }, 'wal', 3, ['wallet', 'x']);
        assert.deepStrictEqual(merged.matches, ['walk', 'walk2', 'wallet']);
    });

    await t.test('does not duplicate a name the helper already offered, nor add anything for an empty token', () => {
        assert.deepStrictEqual(mergeCompletions({ matches: ['walk'], cursor_start: 0 }, 'walk', 4, ['walk']).matches, ['walk']);
        assert.deepStrictEqual(mergeCompletions({ matches: [], cursor_start: 4 }, 'f(x ', 4, ['a']).matches, []);
    });

    await t.test('measures the typed token in code points', () => {
        const merged = mergeCompletions({ matches: [], cursor_start: 3 }, '😀; ab', 5, ['abc']);
        assert.deepStrictEqual(merged.matches, ['abc']);
    });
});

function fakeSession() {
    const calls: string[] = [];
    let exitListener: (() => void) | undefined;
    const session: HelperSession & { calls: string[]; exit: () => void } = {
        calls,
        async execute(code) {
            calls.push(`execute ${code.includes('purrr') ? 'attach purrr' : code}`);
            return { success: true };
        },
        async request<T>(msgType: string) {
            calls.push(`request ${msgType}`);
            return { status: 'ok', found: true } as T;
        },
        async stop() {
            calls.push('stop');
        },
        kill() {
            calls.push('kill');
        },
        on(_event, listener) {
            exitListener = listener;
            return session;
        },
        exit: () => exitListener?.()
    };
    return session;
}

test('RHelper', async (t) => {
    await t.test('starts one process, attaches each package once, and answers', async () => {
        const session = fakeSession();
        let started = 0;
        const helper = new RHelper(async () => {
            started += 1;
            return session;
        });
        const state = { packages: ['purrr'], globals: [] };
        await helper.ask('inspect_request', {}, state);
        await helper.ask('complete_request', {}, state);
        assert.strictEqual(started, 1);
        assert.deepStrictEqual(session.calls, ['execute attach purrr', 'request inspect_request', 'request complete_request']);
    });

    await t.test('starts a new process after the old one exited, and attaches again', async () => {
        const sessions = [fakeSession(), fakeSession()];
        let started = 0;
        const helper = new RHelper(async () => sessions[started++]!);
        const state = { packages: ['purrr'], globals: [] };
        await helper.ask('inspect_request', {}, state);
        sessions[0]!.exit();
        await helper.ask('inspect_request', {}, state);
        assert.strictEqual(started, 2);
        assert.deepStrictEqual(sessions[1]!.calls, ['execute attach purrr', 'request inspect_request']);
    });

    await t.test('a failed start is retried on the next question', async () => {
        let attempts = 0;
        const helper = new RHelper(async () => {
            attempts += 1;
            if (attempts === 1) throw new Error('no R');
            return fakeSession();
        });
        await assert.rejects(helper.ask('inspect_request', {}, { packages: [], globals: [] }), /no R/);
        await helper.ask('inspect_request', {}, { packages: [], globals: [] });
        assert.strictEqual(attempts, 2);
    });

    await t.test('stop() stops the process it started', async () => {
        const session = fakeSession();
        const helper = new RHelper(async () => session);
        await helper.ask('inspect_request', {}, { packages: [], globals: [] });
        await helper.stop();
        assert.ok(session.calls.includes('stop'));
    });
});
