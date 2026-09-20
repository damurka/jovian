import { test } from 'node:test';
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

// Deliberately NO `after(() => process.exit(0))` here (this file used to
// have one, to cope with background ZMQ/R threads keeping the process alive
// after a clean stop). It also swallowed every assertion failure: a subtest
// that failed on purpose still reported "pass 1, fail 0" and exited 0
// (confirmed directly), so this whole file could never actually fail on an
// assertion -- CI's integration step was decorative. `npm run test:integration`
// already passes --test-force-exit, which is the sanctioned way to handle
// lingering handles without discarding the real exit code.

function supervisorExeExists(): boolean {
    const exeName = process.platform === 'win32' ? 'themisto.exe' : 'themisto';
    return existsSync(join(__dirname, '../../dist/native/Release', exeName));
}

function carpoExeExists(): boolean {
    const exeName = process.platform === 'win32' ? 'carpo.exe' : 'carpo';
    return existsSync(join(__dirname, '../../dist/native/Release', exeName));
}

// Same "ask the runtime itself" approach as discoverRHome() above and
// tools/playground/server.js's discoverPythonHome(): sys.prefix is the
// portable PYTHONHOME for whichever Python is actually on PATH. Empty when
// no Python is found, in which case the Python tests below skip themselves.
function discoverPythonHome(): string {
    if (process.env.PYTHONHOME) return process.env.PYTHONHOME;
    for (const cmd of ['python3', 'python']) {
        try {
            return execSync(`${cmd} -c "import sys; print(sys.prefix)"`, { encoding: 'utf8' }).trim();
        } catch {
            // try the next candidate
        }
    }
    return '';
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

    await t.test('R readline() round-trips through the stdin channel', async () => {
        // Regression test: on Windows R.dll has no ptr_R_ReadConsole to
        // assign, so readline() used to fall through to R's own terminal
        // I/O (a hidden console nothing can type into) and block forever;
        // it now goes through an Rstart ReadConsole callback -- see
        // RInterpreter's initEmbeddedRWindows().
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const prompts: string[] = [];
            session.on('input_request', (content: { prompt: string }) => {
                prompts.push(content.prompt);
                session.sendInputReply('World');
            });
            const stdout: string[] = [];
            session.on('stdout', (text: string) => stdout.push(text));

            const result = await session.execute('name <- readline("name? "); cat("hello", name, "\\n")', {
                allowStdin: true,
                timeout: 20000
            });

            assert.strictEqual(result.success, true);
            assert.deepStrictEqual(prompts, ['name? ']);
            assert.ok(stdout.join('').includes('hello World'), `unexpected stdout: ${JSON.stringify(stdout)}`);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R readline() without allowStdin finishes instead of blocking', async () => {
        // The other half of the same guarantee: an execute() that never
        // opted into stdin must not hang the kernel waiting on an
        // input_request nobody is going to answer.
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            let inputRequested = false;
            session.on('input_request', () => { inputRequested = true; });

            const result = await session.execute('z <- readline("q? "); nchar(z)', { timeout: 15000 });

            assert.strictEqual(result.success, true);
            assert.strictEqual(inputRequested, false);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R can plot without crashing the kernel', {
        skip: process.platform !== 'win32' && 'Windows-only: guards the graphapp initialization RInterpreter does itself there'
    }, async () => {
        // Regression test for a real crash found while replacing
        // Rf_initEmbeddedR() on Windows: omitting GA_initapp() made the very
        // first plot() kill the whole R process (hera's default device on
        // Windows is png(), whose "windows" bitmap type is graphapp
        // underneath).
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const result = await session.execute('plot(1:10)', { timeout: 20000 });

            assert.strictEqual(result.success, true);
            assert.ok(
                result.output.some((m) => m.msgType === 'display_data' && m.content?.data?.['image/png']),
                'expected a display_data message carrying an image/png'
            );
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('Python input() round-trips through the stdin channel', async (t2) => {
        const pythonHome = discoverPythonHome();
        if (!pythonHome || !carpoExeExists()) {
            t2.skip('no Python installation found, or carpo not built');
            return;
        }

        // Regression test for the original "input() hangs forever" bug:
        // KernelCore::sendStdin() addresses the stdin ROUTER using the
        // identity it captured from the *shell* channel, which only reaches
        // the client if its shell and stdin DEALER sockets share one ZMQ
        // routing id (ClientZmqImpl's constructor).
        const manager = new SessionManager();
        const session = await manager.createSession({ kernelType: 'python', pythonHome });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const prompts: string[] = [];
            const answers = ['5', '7'];
            session.on('input_request', (content: { prompt: string }) => {
                prompts.push(content.prompt);
                session.sendInputReply(answers[prompts.length - 1]);
            });
            const stdout: string[] = [];
            session.on('stdout', (text: string) => stdout.push(text));

            const result = await session.execute(
                'a = float(input("first: "))\nb = float(input("second: "))\nprint("sum is", a + b)',
                { allowStdin: true, timeout: 20000 }
            );

            assert.strictEqual(result.success, true);
            assert.deepStrictEqual(prompts, ['first: ', 'second: ']);
            assert.ok(stdout.join('').includes('sum is 12.0'), `unexpected stdout: ${JSON.stringify(stdout)}`);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('Session.getHistory() and queryKernelHistory() reflect real executions', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            await session.execute('1 + 1');
            await session.execute('2 + 2');

            const local = session.getHistory();
            assert.deepStrictEqual(local.map((entry) => entry.code), ['1 + 1', '2 + 2']);

            const kernel = await session.queryKernelHistory();
            assert.deepStrictEqual(kernel.map((entry) => entry[2]), ['1 + 1', '2 + 2']);
        } finally {
            await manager.stopAll();
        }
    });
});
