import { test } from 'node:test';
import * as assert from 'node:assert';
import { buildSessionOptionsBody } from '../../../dist/lib/session/supervisor-client.js';

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
});
