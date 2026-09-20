import { test } from 'node:test';
import * as assert from 'node:assert';
import { existsSync, mkdtempSync, realpathSync, rmSync } from 'fs';
import { tmpdir } from 'os';
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
            // (tools/playground). EventEmitter throws an
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

    // -- Jupyter protocol requests, end to end over supervisor + kernel ----

    // Resolves with the first `eventName` event whose content satisfies
    // `predicate` (or rejects after `timeoutMs`) -- for events like
    // comm_msg that the kernel pushes rather than replies to.
    function waitForEvent<T = any>(
        session: { on: (e: string, cb: (c: T) => void) => unknown; off: (e: string, cb: (c: T) => void) => unknown },
        eventName: string,
        predicate: (content: T) => boolean = () => true,
        timeoutMs = 15000
    ): Promise<T> {
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                session.off(eventName, listener);
                reject(new Error(`timed out waiting for a "${eventName}" event`));
            }, timeoutMs);
            const listener = (content: T) => {
                if (!predicate(content)) return;
                clearTimeout(timer);
                session.off(eventName, listener);
                resolve(content);
            };
            session.on(eventName, listener);
        });
    }

    await t.test('R: kernelInfo(), complete(), inspect(), isComplete() answer over the protocol', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const info = await session.kernelInfo();
            assert.strictEqual(info.status, 'ok');
            assert.strictEqual(info.language_info.name, 'R');
            assert.match(info.language_info.version, /^\d+\.\d+/);
            assert.ok(info.protocol_version);

            const completion = await session.complete('pri');
            assert.ok(completion.matches.includes('print'), `completing "pri": ${JSON.stringify(completion.matches)}`);

            const inspection = await session.inspect('mean');
            assert.strictEqual(inspection.status, 'ok');

            assert.strictEqual((await session.isComplete('1 +')).status, 'incomplete');
            assert.strictEqual((await session.isComplete('1 + 1')).status, 'complete');
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: userExpressions are evaluated after the code and each reports its own failure', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});

            const result = await session.execute('x <- 21', {
                userExpressions: { double: 'x * 2', boom: 'stop("kaboom")' }
            });

            assert.strictEqual(result.success, true);
            assert.ok(result.userExpressions, 'expected userExpressions on the result');
            const double = result.userExpressions.double;
            assert.strictEqual(double.status, 'ok');
            assert.match((double as any).data['text/plain'], /42/);
            const boom = result.userExpressions.boom;
            assert.strictEqual(boom.status, 'error');
            assert.match((boom as any).evalue, /kaboom/);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: stopOnError aborts the executions queued behind a failure, and a normal failure does not', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});

            const [first, second, third] = await Promise.all([
                session.execute('stop("first fails")', { stopOnError: true }),
                session.execute('1 + 1'),
                session.execute('2 + 2')
            ]);
            assert.strictEqual(first.success, false);
            assert.strictEqual(second.aborted, true);
            assert.strictEqual(third.aborted, true);

            // The session is still perfectly usable afterwards...
            assert.strictEqual((await session.execute('3 + 3')).success, true);

            // ...and without stopOnError, later work still runs.
            const [failed, ran] = await Promise.all([
                session.execute('stop("fails")'),
                session.execute('4 + 4')
            ]);
            assert.strictEqual(failed.success, false);
            assert.strictEqual(ran.success, true);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: interrupt() is acknowledged over the control channel', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            assert.strictEqual(await session.interrupt(), true);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: stop() surfaces the kernel\'s shutdown_reply (restart:false) and is not reported as a crash', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        const replies: Array<{ status: string; restart: boolean }> = [];
        session.on('shutdown_reply', (content) => replies.push(content));
        session.on('exit', ({ reason }) => assert.fail(`stop was reported as a crash: ${reason}`));

        await session.stop();
        assert.deepStrictEqual(replies.map((r) => [r.status, r.restart]), [['ok', false]]);
        await manager.stopAll();
    });

    await t.test('R: restart() sends the kernel a shutdown_request with restart:true and yields a fresh working kernel', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));
            session.on('exit', ({ reason }) => assert.fail(`restart was reported as a crash: ${reason}`));

            await session.execute('kept <- 1');

            const replies: Array<{ status: string; restart: boolean }> = [];
            session.on('shutdown_reply', (content) => replies.push(content));
            await session.restart();
            assert.deepStrictEqual(replies.map((r) => [r.status, r.restart]), [['ok', true]]);

            // A genuinely new interpreter: the old one's variable is gone.
            const result = await session.execute('exists("kept")');
            assert.strictEqual(result.success, true);
            assert.ok(result.output.some((m) => m.content?.data?.['text/plain']?.includes('FALSE')));
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: comm_open for an unknown target is answered with a comm_close; comm_info_request works', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            const closed = waitForEvent<{ comm_id: string }>(session, 'comm_close', (c) => c.comm_id === 'nope-1');
            await session.commOpen('no_such_target', {}, 'nope-1');
            await closed;

            const info = await session.commInfo('no_such_target');
            assert.strictEqual(info.status, 'ok');
            assert.deepStrictEqual(info.comms, {});
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: a comm registered in the kernel round-trips comm_open/comm_msg/comm_close with the client', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const registered = await session.execute(`
hera::CommManager$register_comm_target("echo", function(comm, message) {
    comm$on_message(function(msg) {
        comm$send(list(echo = msg$content$data$text))
    })
    comm$send(list(hello = "from R"))
})
`);
            assert.strictEqual(registered.success, true);

            const greeting = waitForEvent<{ comm_id: string; data: { hello?: string } }>(
                session, 'comm_msg', (c) => c.data?.hello === 'from R');
            const { commId } = await session.commOpen('echo', { from: 'client' });
            assert.strictEqual((await greeting).comm_id, commId);

            const echoed = waitForEvent<{ data: { echo?: string } }>(session, 'comm_msg', (c) => c.data?.echo === 'ping');
            await session.commMsg(commId, { text: 'ping' });
            await echoed;

            const open = await session.commInfo('echo');
            assert.deepStrictEqual(Object.keys(open.comms), [commId]);

            await session.commClose(commId);
            // comm_close is fire-and-forget; comm_info is a request the
            // kernel answers only after handling the close (same thread, in
            // order), so by the time it replies the comm is gone.
            const after = await session.commInfo('echo');
            assert.deepStrictEqual(after.comms, {});
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: executionState tracks the kernel\'s busy/idle status', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            await session.execute('1');
            // The trailing idle status can land just after the execute_reply.
            await new Promise((resolve) => setTimeout(resolve, 200));
            assert.strictEqual(session.executionState, 'idle');
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('Python: kernelInfo(), complete(), isComplete(), userExpressions, stopOnError', async (t2) => {
        const pythonHome = discoverPythonHome();
        if (!pythonHome || !carpoExeExists()) {
            t2.skip('no Python installation found, or carpo not built');
            return;
        }

        const manager = new SessionManager();
        const session = await manager.createSession({ kernelType: 'python', pythonHome });

        try {
            session.on('error', () => {});

            const info = await session.kernelInfo();
            assert.strictEqual(info.language_info.name, 'python');

            const completion = await session.complete('pri');
            // Python's completer appends the call paren ("print(").
            assert.ok(completion.matches.some((m) => m.startsWith('print')), `completing "pri": ${JSON.stringify(completion.matches)}`);

            assert.strictEqual((await session.isComplete('for i in range(3):')).status, 'incomplete');
            assert.strictEqual((await session.isComplete('1 + 1')).status, 'complete');

            const withExpressions = await session.execute('x = 21', {
                userExpressions: { double: 'x * 2', boom: 'undefined_name' }
            });
            assert.strictEqual(withExpressions.success, true);
            assert.strictEqual(withExpressions.userExpressions!.double.status, 'ok');
            assert.strictEqual((withExpressions.userExpressions!.double as any).data['text/plain'], '42');
            assert.strictEqual(withExpressions.userExpressions!.boom.status, 'error');
            assert.strictEqual((withExpressions.userExpressions!.boom as any).ename, 'NameError');

            const [first, second] = await Promise.all([
                session.execute('raise ValueError("first fails")', { stopOnError: true }),
                session.execute('1 + 1')
            ]);
            assert.strictEqual(first.success, false);
            assert.strictEqual(second.aborted, true);

            const replies: Array<{ restart: boolean }> = [];
            session.on('shutdown_reply', (content) => replies.push(content));
            session.on('exit', ({ reason }) => assert.fail(`restart was reported as a crash: ${reason}`));
            await session.restart();
            assert.deepStrictEqual(replies.map((r) => r.restart), [true]);
            assert.strictEqual((await session.execute('1 + 1')).success, true);
        } finally {
            await manager.stopAll();
        }
    });

    // -- interrupt, working directory, comms, stderr -----------------------

    await t.test('R: interrupt() breaks out of a running sleep and a busy loop, and the session stays usable', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});

            for (const code of ['Sys.sleep(60)', 'x <- 0; while (TRUE) x <- x + 1']) {
                const started = Date.now();
                const running = session.execute(code, { timeout: 60000 });
                await new Promise((resolve) => setTimeout(resolve, 500));

                assert.strictEqual(await session.interrupt({ timeout: 5000 }), true, `interrupt not acknowledged for: ${code}`);
                const result = await running;

                assert.strictEqual(result.success, false, `"${code}" should have been interrupted`);
                assert.ok(Date.now() - started < 20000, `"${code}" took ${Date.now() - started}ms to stop`);
            }

            const after = await session.execute('1 + 1');
            assert.strictEqual(after.success, true);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('Python: interrupt() raises KeyboardInterrupt in a running sleep and a busy loop', async (t2) => {
        const pythonHome = discoverPythonHome();
        if (!pythonHome || !carpoExeExists()) {
            t2.skip('no Python installation found, or carpo not built');
            return;
        }

        const manager = new SessionManager();
        const session = await manager.createSession({ kernelType: 'python', pythonHome });

        try {
            session.on('error', () => {});

            for (const code of ['import time\ntime.sleep(60)', 'while True:\n    pass']) {
                const started = Date.now();
                const running = session.execute(code, { timeout: 60000 });
                await new Promise((resolve) => setTimeout(resolve, 500));

                assert.strictEqual(await session.interrupt({ timeout: 5000 }), true, `interrupt not acknowledged for: ${code}`);
                const result = await running;

                assert.strictEqual(result.success, false, `"${code}" should have been interrupted`);
                assert.match(String(result.error?.message ?? ''), /KeyboardInterrupt|^$/);
                assert.ok(result.output.some((m) => m.msgType === 'error' && m.content?.ename === 'KeyboardInterrupt'),
                    `expected a KeyboardInterrupt error for: ${code}`);
                assert.ok(Date.now() - started < 20000, `"${code}" took ${Date.now() - started}ms to stop`);
            }

            assert.strictEqual((await session.execute('1 + 1')).success, true);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: workingDirectory is where the kernel starts, and survives a restart', async () => {
        const dir = mkdtempSync(join(tmpdir(), 'jovian-cwd-'));
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome(), workingDirectory: dir });

        try {
            session.on('error', () => {});
            const cwdOf = async () => {
                const result = await session.execute('getwd()');
                const text = String(result.output.find((m) => m.msgType === 'execute_result')?.content?.data?.['text/plain'] ?? '');
                const quoted = /"(.*)"/.exec(text)?.[1] ?? '';
                return realpathSync.native(quoted);
            };

            assert.strictEqual(await cwdOf(), realpathSync.native(dir));

            await session.restart();
            assert.strictEqual(await cwdOf(), realpathSync.native(dir), 'restart lost the working directory');
        } finally {
            await manager.stopAll();
            rmSync(dir, { recursive: true, force: true });
        }
    });

    await t.test('Python: workingDirectory is where the kernel starts', async (t2) => {
        const pythonHome = discoverPythonHome();
        if (!pythonHome || !carpoExeExists()) {
            t2.skip('no Python installation found, or carpo not built');
            return;
        }

        const dir = mkdtempSync(join(tmpdir(), 'jovian-cwd-'));
        const manager = new SessionManager();
        const session = await manager.createSession({ kernelType: 'python', pythonHome, workingDirectory: dir });

        try {
            session.on('error', () => {});
            const out: string[] = [];
            session.on('stdout', (text: string) => out.push(text));

            await session.execute('import os\nprint(os.getcwd())');
            assert.strictEqual(realpathSync.native(out.join('').trim()), realpathSync.native(dir));
        } finally {
            await manager.stopAll();
            rmSync(dir, { recursive: true, force: true });
        }
    });

    await t.test('a workingDirectory that does not exist is a clear createSession() error', async () => {
        const manager = new SessionManager();
        try {
            await assert.rejects(
                manager.createSession({ rHome: discoverRHome(), workingDirectory: join(tmpdir(), 'jovian-no-such-dir-xyz') }),
                /workingDirectory does not exist/
            );
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: stderr output is emitted as "stderr" and stdout as "stdout"', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});
            const out: string[] = [];
            const err: string[] = [];
            session.on('stdout', (text: string) => out.push(text));
            session.on('stderr', (text: string) => err.push(text));

            await session.execute('cat("to-stdout\\n"); message("to-stderr")');
            assert.ok(out.join('').includes('to-stdout'), `stdout was ${JSON.stringify(out)}`);
            assert.ok(err.join('').includes('to-stderr'), `stderr was ${JSON.stringify(err)}`);
            assert.ok(!out.join('').includes('to-stderr'), 'stderr leaked into stdout');
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: a comm the KERNEL opens arrives as a "comm" event, and Comm.send() reaches it', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            const opened = new Promise<{ comm: any; data: any }>((resolve) =>
                session.once('comm', (comm, data) => resolve({ comm, data })));

            const registered = await session.execute(`
hera::CommManager$register_comm_target("kernel_side")
comm <- hera::CommManager$new_comm("kernel_side")
comm$on_message(function(msg) { comm$send(list(echo = msg$content$data$text)) })
comm$open(list(greeting = "from R"))
comm$send(list(second = 2))
`);
            assert.strictEqual(registered.success, true);

            const { comm, data } = await opened;
            assert.strictEqual(comm.targetName, 'kernel_side');
            assert.deepStrictEqual(data, { greeting: 'from R' });

            // Listeners attach after the open; the follow-up comm_msg may
            // already have been delivered, so only the echo is asserted.
            const echoed = new Promise<any>((resolve) => comm.on('message', (d: any) => { if (d.echo) resolve(d); }));
            await comm.send({ text: 'ping' });
            assert.deepStrictEqual(await echoed, { echo: 'ping' });

            const closed = new Promise((resolve) => comm.once('close', resolve));
            await comm.close();
            await closed;
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: openComm() to a kernel-registered target round-trips messages through the Comm object', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', (error) => assert.fail(`session reported an error: ${error?.message ?? error}`));

            await session.execute(`
hera::CommManager$register_comm_target("echo2", function(comm, message) {
    comm$on_message(function(msg) { comm$send(list(echo = msg$content$data$text)) })
})
`);

            const comm = await session.openComm('echo2');
            const echoed = new Promise<any>((resolve) => comm.once('message', resolve));
            await comm.send({ text: 'hello' });
            assert.deepStrictEqual(await echoed, { echo: 'hello' });

            await comm.close();
            assert.strictEqual(comm.closed, true);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: an execution that times out is interrupted, so the kernel is usable straight away', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});

            const started = Date.now();
            await assert.rejects(session.execute('Sys.sleep(120)', { timeout: 1500 }), /timed out.*interrupted/);

            // Without the interrupt this would queue behind the sleep for two minutes.
            const after = await session.execute('1 + 1', { timeout: 20000 });
            assert.strictEqual(after.success, true);
            assert.ok(Date.now() - started < 20000, `took ${Date.now() - started}ms`);
        } finally {
            await manager.stopAll();
        }
    });

    // Output must reach the client WHILE one long expression runs, not when it
    // ends, and a flood of tiny writes must not become a flood of messages.
    // When (ms since epoch) the cumulative stdout first contained `needle` --
    // chunk boundaries are up to the kernel, so a token can be split across two.
    function firstSeenAt(arrivals: Array<{ at: number; text: string }>, needle: string): number | undefined {
        let text = '';
        for (const a of arrivals) {
            text += a.text;
            if (text.includes(needle)) return a.at;
        }
        return undefined;
    }

    async function checkStreaming(session: any, code: string, expectedLast: RegExp) {
        const arrivals: Array<{ at: number; text: string }> = [];
        let streamMessages = 0;
        session.on('stdout', (text: string) => arrivals.push({ at: Date.now(), text }));
        session.on('message', (m: { msgType: string }) => { if (m.msgType === 'stream') streamMessages++; });

        const started = Date.now();
        const result = await session.execute(code, { timeout: 60000 });
        const finished = Date.now();

        assert.strictEqual(result.success, true);
        return { arrivals, streamMessages, started, finished };
    }

    await t.test('R: stdout streams live from inside a single long expression', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});
            const { arrivals, started, finished } = await checkStreaming(
                session, 'for (i in 1:6) { cat("tick", i, "\\n"); Sys.sleep(0.4) }', /tick 6/);

            const all = arrivals.map((a) => a.text).join('');
            assert.ok(all.includes('tick 1') && all.includes('tick 6'), `stdout was ${JSON.stringify(all)}`);
            const firstAt = firstSeenAt(arrivals, 'tick 1')!;
            assert.ok(firstAt - started < (finished - started) - 1000,
                `tick 1 arrived at +${firstAt - started}ms of ${finished - started}ms: buffered until the end`);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: a flood of print() calls arrives complete, in order, and coalesced', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});
            const { arrivals, streamMessages } = await checkStreaming(session, 'for (i in 1:30000) print(i)', /30000/);

            const text = arrivals.map((a) => a.text).join('');
            const lines = text.split(/\r?\n/).filter((l) => l.length > 0);
            assert.strictEqual(lines.length, 30000, `got ${lines.length} lines`);
            assert.strictEqual(lines[0], '[1] 1');
            assert.strictEqual(lines[29999], '[1] 30000');
            assert.ok(streamMessages < 5000, `${streamMessages} stream messages for 30000 prints: not coalesced`);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('Python: stdout streams live and a flood of print() calls is coalesced', async (t2) => {
        const pythonHome = discoverPythonHome();
        if (!pythonHome || !carpoExeExists()) {
            t2.skip('no Python installation found, or carpo not built');
            return;
        }

        const manager = new SessionManager();
        const session = await manager.createSession({ kernelType: 'python', pythonHome });

        try {
            session.on('error', () => {});
            const live = await checkStreaming(session, 'import time\nfor i in range(6):\n    print("tick", i)\n    time.sleep(0.4)', /tick 5/);
            const firstAt = firstSeenAt(live.arrivals, 'tick 0')!;
            assert.ok(firstAt - live.started < (live.finished - live.started) - 1000, 'tick 0 was buffered until the end');

            const flood = await checkStreaming(session, 'for i in range(30000):\n    print(i)', /29999/);
            const lines = flood.arrivals.map((a) => a.text).join('').split(/\r?\n/).filter((l) => l.length > 0);
            assert.strictEqual(lines.length, 30000);
            assert.strictEqual(lines[29999], '29999');
            assert.ok(flood.streamMessages < 5000, `${flood.streamMessages} stream messages for 30000 prints`);
        } finally {
            await manager.stopAll();
        }
    });

    await t.test('R: status() reports pid, working directory and a heartbeat that stays live while the kernel is busy', async () => {
        const manager = new SessionManager();
        const session = await manager.createSession({ rHome: discoverRHome() });

        try {
            session.on('error', () => {});

            let status = await session.status();
            for (let i = 0; i < 100 && !status.heartbeat?.hasPong; i++) {
                await new Promise((resolve) => setTimeout(resolve, 100));
                status = await session.status();
            }
            assert.strictEqual(status.status, 'ready');
            assert.strictEqual(status.kernelType, 'r');
            assert.ok(status.pid > 0);
            assert.strictEqual(status.heartbeat?.hasPong, true);
            assert.ok(status.heartbeat!.rttMs >= 0 && status.heartbeat!.rttMs < 1000);

            const busy = session.execute('Sys.sleep(3)');
            await new Promise((resolve) => setTimeout(resolve, 1500));
            const during = await session.status();
            assert.ok(during.heartbeat!.sinceLastPongMs < 1500, `heartbeat went stale while busy: ${JSON.stringify(during.heartbeat)}`);
            assert.strictEqual(during.heartbeat!.misses, 0);
            await busy;
        } finally {
            await manager.stopAll();
        }
    });
});
