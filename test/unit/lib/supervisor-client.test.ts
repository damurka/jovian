import { test } from 'node:test';
import * as assert from 'node:assert';
import { buildSessionOptionsBody, SupervisorClient } from '../../../dist/lib/session/supervisor-client.js';
import { Logger } from '../../../dist/lib/utils/logger.js';

// Covers exactly the request-body shape POST /sessions and .../restart send
// to the supervisor (native/src/themisto/http_api.cpp's parseSessionOptions())
// -- pulled out of createSession()/restartSession() into this one pure
// function specifically so it's testable without mocking fetch() or
// spawning a real themisto.exe.
test('buildSessionOptionsBody', async (t) => {
    await t.test('carries kernelType and the R fields for an R session', () => {
        const body = buildSessionOptionsBody({
            kernelType: 'r',
            rHome: '/opt/R',
            rPath: '/opt/R/bin',
            rLibs: '/opt/R/library'
        });

        assert.strictEqual(body.kernelType, 'r');
        assert.strictEqual(body.rHome, '/opt/R');
        assert.strictEqual(body.rPath, '/opt/R/bin');
        assert.strictEqual(body.rLibs, '/opt/R/library');
    });

    await t.test('carries kernelType and the Python fields for a python session', () => {
        const body = buildSessionOptionsBody({
            kernelType: 'python',
            pythonHome: '/usr',
            pythonPath: '/usr/lib/python3.12',
            venvPath: '/home/user/.venv'
        });

        assert.strictEqual(body.kernelType, 'python');
        assert.strictEqual(body.pythonHome, '/usr');
        assert.strictEqual(body.pythonPath, '/usr/lib/python3.12');
        assert.strictEqual(body.venvPath, '/home/user/.venv');
    });

    await t.test('carries kernelType and the Stata fields for a stata session', () => {
        const body = buildSessionOptionsBody({
            kernelType: 'stata',
            stataHome: '/usr/local/stata19',
            stataEdition: 'se'
        });

        assert.strictEqual(body.kernelType, 'stata');
        assert.strictEqual(body.stataHome, '/usr/local/stata19');
        assert.strictEqual(body.stataEdition, 'se');
    });

    await t.test('drops unset fields entirely once JSON-serialized, rather than sending them as null', () => {
        // JSON.stringify() omits undefined-valued keys -- this is what lets
        // http_api.cpp's parseSessionOptions() use body.value("rHome", "")
        // defaults correctly for a python session that never set any R
        // field, and vice versa.
        const body = buildSessionOptionsBody({ kernelType: 'python', pythonHome: '/usr' });
        const serialized = JSON.parse(JSON.stringify(body));

        assert.strictEqual('rHome' in serialized, false);
        assert.strictEqual('rPath' in serialized, false);
        assert.strictEqual('pandocPath' in serialized, false);
    });

    await t.test('omits kernelType when not given, matching the server default of "r"', () => {
        const body = buildSessionOptionsBody({ rHome: '/opt/R' });
        const serialized = JSON.parse(JSON.stringify(body));

        assert.strictEqual('kernelType' in serialized, false);
    });

    await t.test('passes workingDirectory through to the supervisor', () => {
        const body = buildSessionOptionsBody({ rHome: '/opt/R', workingDirectory: '/projects/a' });
        assert.strictEqual(body.workingDirectory, '/projects/a');
    });
});

// A kernel that dies mid-session (exit code 1 from elara's last-resort handler)
// said why on stderr; the exit reason carries that, not only the exit code.
test('describeKernelExit', async (t) => {
    // rememberOutput() is what the supervisor's stderr reader calls for each line
    const withOutput = (lines: { text: string; ageMs: number }[], options = {}) => {
        const client = new SupervisorClient(new Logger(undefined, 'silent'), options) as unknown as {
            recentOutput: { text: string; at: number }[];
            describeKernelExit(reason: string): string;
        };
        for (const line of lines) client.recentOutput.push({ text: line.text, at: Date.now() - line.ageMs });
        return client;
    };
    const exit = 'kernel process exited unexpectedly (process exited with code 0x1)';

    await t.test('adds the error lines the kernel printed just before', () => {
        const client = withOutput([
            { text: '[elara] registered with supervisor at 127.0.0.1', ageMs: 1000 },
            { text: '[elara] FATAL: bad allocation', ageMs: 500 }
        ]);
        assert.strictEqual(client.describeKernelExit(exit), `${exit}\nKernel output:\n  [elara] FATAL: bad allocation`);
    });

    await t.test('leaves out old lines and lines that are not errors', () => {
        const client = withOutput([
            { text: '[elara] FATAL: an earlier kernel', ageMs: 60_000 },
            { text: '[elara] ready', ageMs: 100 }
        ]);
        assert.strictEqual(client.describeKernelExit(exit), exit);
    });

    await t.test('adds nothing when the kernel output was already printed', () => {
        const client = withOutput([{ text: '[elara] FATAL: bad allocation', ageMs: 100 }], { forwardKernelOutput: true });
        assert.strictEqual(client.describeKernelExit(exit), exit);
    });
});
