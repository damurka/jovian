import { test } from 'node:test';
import * as assert from 'node:assert';
import {
    discoverPythonHome,
    discoverRHome,
    withDiscoveredRuntime,
    type DiscoveryContext
} from '../../../dist/lib/session/runtimes.js';

// answers maps "command arg arg" to the output that command would print;
// anything not listed fails, like a command that is not installed.
function context(answers: Record<string, string>, overrides: Partial<DiscoveryContext> = {}): DiscoveryContext & { calls: string[] } {
    const calls: string[] = [];
    return {
        env: {},
        platform: 'linux',
        run: (command, args) => {
            const key = [command, ...args].join(' ');
            calls.push(key);
            return answers[key];
        },
        calls,
        ...overrides
    };
}

test('discoverRHome', async (t) => {
    await t.test('prefers R_HOME and runs nothing', () => {
        const ctx = context({ 'R RHOME': '/usr/lib/R' }, { env: { R_HOME: '/opt/R' } });
        assert.strictEqual(discoverRHome(ctx), '/opt/R');
        assert.deepStrictEqual(ctx.calls, []);
    });

    await t.test('asks R itself when R_HOME is not set', () => {
        assert.strictEqual(discoverRHome(context({ 'R RHOME': '/usr/lib/R' })), '/usr/lib/R');
    });

    await t.test('reads the Windows registry when R is not on PATH', () => {
        const ctx = context({
            'reg query HKLM\\SOFTWARE\\R-core\\R /v InstallPath': '    InstallPath    REG_SZ    C:\\Program Files\\R\\R-4.6.0'
        }, { platform: 'win32' });
        assert.strictEqual(discoverRHome(ctx), 'C:\\Program Files\\R\\R-4.6.0');
    });

    await t.test('does not look at the registry off Windows', () => {
        const ctx = context({});
        assert.strictEqual(discoverRHome(ctx), undefined);
        assert.ok(!ctx.calls.some((call) => call.startsWith('reg ')));
    });

    await t.test('is undefined when R cannot be found', () => {
        assert.strictEqual(discoverRHome(context({}, { platform: 'win32' })), undefined);
    });
});

test('discoverPythonHome', async (t) => {
    const script = 'import sys; print(sys.base_prefix)';

    await t.test('prefers PYTHONHOME', () => {
        assert.strictEqual(discoverPythonHome(context({}, { env: { PYTHONHOME: '/opt/py' } })), '/opt/py');
    });

    await t.test('uses python3, and its base prefix rather than a venv prefix', () => {
        assert.strictEqual(discoverPythonHome(context({ [`python3 -c ${script}`]: '/usr' })), '/usr');
    });

    await t.test('falls back to python, then to the py launcher on Windows', () => {
        assert.strictEqual(discoverPythonHome(context({ [`python -c ${script}`]: '/usr/local' })), '/usr/local');
        assert.strictEqual(
            discoverPythonHome(context({ [`py -3 -c ${script}`]: 'C:\\Python312' }, { platform: 'win32' })),
            'C:\\Python312'
        );
    });

    await t.test('is undefined when there is no Python', () => {
        assert.strictEqual(discoverPythonHome(context({})), undefined);
    });
});

test('withDiscoveredRuntime', async (t) => {
    await t.test('fills in rHome for an R session (the default kernel)', () => {
        assert.deepStrictEqual(
            withDiscoveredRuntime({ workingDirectory: '/p' }, context({ 'R RHOME': '/usr/lib/R' })),
            { workingDirectory: '/p', rHome: '/usr/lib/R' }
        );
    });

    await t.test('fills in pythonHome for a Python session and leaves rHome alone', () => {
        const found = withDiscoveredRuntime(
            { kernelType: 'python' },
            context({ 'python3 -c import sys; print(sys.base_prefix)': '/usr', 'R RHOME': '/usr/lib/R' })
        );
        assert.deepStrictEqual(found, { kernelType: 'python', pythonHome: '/usr' });
    });

    await t.test('never overrides what the caller passed, and runs nothing', () => {
        const ctx = context({ 'R RHOME': '/usr/lib/R' });
        const given = { kernelType: 'r' as const, rHome: '/my/R' };
        assert.strictEqual(withDiscoveredRuntime(given, ctx), given);
        assert.deepStrictEqual(ctx.calls, []);
    });

    await t.test('leaves the options as they are when nothing is found', () => {
        const given = { kernelType: 'python' as const };
        assert.strictEqual(withDiscoveredRuntime(given, context({})), given);
    });
});
