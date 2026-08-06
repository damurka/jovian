import { test, after } from 'node:test';
import * as assert from 'node:assert';
import { existsSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { SessionManager } from '../../dist/lib/session/session-manager.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// See test/integration/engine.test.ts for why: background ZMQ/R threads can
// leave something running that node --test's own teardown never notices,
// even after a clean stop -- harmless safety net, not evidence of a bug in
// this test if it's needed.
after(() => {
    process.exit(0);
});

function supervisorExeExists(): boolean {
    const exeName = process.platform === 'win32' ? 'datasuite-supervisor.exe' : 'datasuite-supervisor';
    return existsSync(join(__dirname, '../../dist/native/Release', exeName));
}

test('SessionManager Integration (supervisor + standalone kernel exe)', async (t) => {
    if (!supervisorExeExists()) {
        console.log('Skipping integration test: datasuite-supervisor executable not built');
        return;
    }

    await t.test('creates a session end-to-end and executes R code over the supervisor', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({
            rHome: process.env.R_HOME || 'C:/Program Files/R/R-4.6.0'
        });

        try {
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
            rHome: process.env.R_HOME || 'C:/Program Files/R/R-4.6.0'
        });

        try {
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
