import { test, after } from 'node:test';
import * as assert from 'node:assert';
import { existsSync } from 'fs';
import { execSync } from 'node:child_process';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { SessionManager } from '../../dist/lib/session/session-manager.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// R_HOME isn't set as an inherited env var by r-lib/actions/setup-r (or by
// many other R installs) -- confirmed the hard way, via a real CI failure
// on Linux/macOS ("cannot find system Renviron" / "unable to open the base
// package") once this fell through to a hardcoded Windows-only path.
// `R RHOME` is R's own portable way of answering this on every platform
// (the same technique cmake/FindR.cmake already uses); the hardcoded path
// remains only as a last resort for a Windows machine with R installed but
// not on PATH at all.
function discoverRHome(): string {
    if (process.env.R_HOME) return process.env.R_HOME;
    try {
        return execSync('R RHOME', { encoding: 'utf8' }).trim();
    } catch {
        return 'C:/Program Files/R/R-4.6.0';
    }
}

// See test/integration/engine.test.ts for why: background ZMQ/R threads can
// leave something running that node --test's own teardown never notices,
// even after a clean stop -- harmless safety net, not evidence of a bug in
// this test if it's needed.
after(() => {
    process.exit(0);
});

function supervisorExeExists(): boolean {
    const exeName = process.platform === 'win32' ? 'themisto.exe' : 'themisto';
    return existsSync(join(__dirname, '../../dist/native/Release', exeName));
}

test('SessionManager Integration (supervisor + standalone kernel exe)', async (t) => {
    if (!supervisorExeExists()) {
        console.log('Skipping integration test: themisto executable not built');
        return;
    }

    await t.test('creates a session end-to-end and executes R code over the supervisor', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({
            rHome: discoverRHome()
        });

        try {
            // Session forwards a Jupyter "error" message (a normal, standard
            // msg_type -- see MessageRouter.route) as Node's own special
            // 'error' event, matching what the playground already relies on
            // (tools/playground/server.js/repl.js). EventEmitter throws an
            // unhandled exception if that specific event has no listener at
            // all, which -- confirmed the hard way, locally -- kills the
            // whole test process outright rather than failing this one
            // assertion. Fail with a readable message instead.
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const result = await session.execute('1 + 1');
            assert.strictEqual(result.success, true);
            assert.strictEqual(result.output.some((m) => m.content?.data?.['text/plain']?.includes('2')), true);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('emits stdout for print() output', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({
            rHome: discoverRHome()
        });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const stdout: string[] = [];
            session.on('stdout', (text: string) => stdout.push(text));

            const result = await session.execute('cat("hello from R\\n")');
            assert.strictEqual(result.success, true);
            assert.ok(stdout.some((chunk) => chunk.includes('hello from R')));
        } finally {
            await manager.stopAll();
        }
    });
});
