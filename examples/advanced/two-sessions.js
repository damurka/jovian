// Demonstrates the point of Jovian's process-per-session architecture:
// each session spawns its own `elara` process via `themisto`, so one
// session blocking on a long-running R call never starves another --
// they're different embedded R interpreters entirely, not two objects
// sharing one. Ports are auto-assigned per session
// (find_free_port() in native/src/adrastea/transport/common/middleware.cpp),
// so nothing here collides regardless of how many sessions run at once.
//
// Session 1 runs a slow, blocking call (Sys.sleep()); Session 2 fires off
// quick execute() calls concurrently and gets replies immediately,
// without waiting for Session 1 to finish.
//
// Run from the repo root after `npm run build`:
//   node examples/advanced/two-sessions.js
//
// Set R_HOME (and R_PATH/R_LIBS if needed) to your own R installation --
// see examples/basic/simple-execute.js for the minimal single-session form.
import { SessionManager } from '../../dist/lib/index.js';

const manager = new SessionManager();

const rOptions = {
    rHome: process.env.R_HOME,
    rPath: process.env.R_PATH,
    rLibs: process.env.R_LIBS
    // Note: a `logger` callback can't cross the process boundary to a
    // session's child process -- use the 'message'/'stdout' events below instead.
};

async function runSession1(session) {
    session.on('stdout', (text) => process.stdout.write(`[Session 1] ${text}`));

    console.log('\n=== Session 1: starting a 5s blocking call ===');
    const start = Date.now();
    await session.execute('Sys.sleep(5); print("Session 1 done sleeping")');
    console.log(`[Session 1] finished after ${((Date.now() - start) / 1000).toFixed(1)}s`);
}

async function runSession2(session) {
    session.on('stdout', (text) => process.stdout.write(`[Session 2] ${text}`));

    console.log('\n=== Session 2: running quick calls concurrently with Session 1 ===');
    const start = Date.now();

    const helloResult = await session.execute('print("Hello from Session 2!")');
    console.log(`[Session 2] print() result: ${helloResult.success ? 'ok' : 'error'} (+${((Date.now() - start) / 1000).toFixed(1)}s)`);

    const mathResult = await session.execute('1 + 1');
    console.log(`[Session 2] 1 + 1 result: ${mathResult.success ? 'ok' : 'error'} (+${((Date.now() - start) / 1000).toFixed(1)}s)`);
}

// A session actively running a blocking call (Session 1's Sys.sleep())
// can't process a graceful shutdown_request until that call returns -- its
// R interpreter thread is the same thread that would need to notice the
// request. So Ctrl+C tries a graceful stopAll() first, but falls back to
// forcibly killing every session's process after a timeout, so exiting is
// never blocked on a session that's busy.
let shuttingDown = false;
process.on('SIGINT', async () => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log('\nShutting down...');

    const timeout = setTimeout(() => {
        console.log('Graceful shutdown timed out -- force-killing sessions.');
        manager.killAll();
        process.exit(0);
    }, 5000);

    await manager.stopAll();
    clearTimeout(timeout);
    process.exit(0);
});

const session1 = await manager.createSession(rOptions);
const session2 = await manager.createSession(rOptions);

await Promise.all([runSession1(session1), runSession2(session2)]);
await manager.stopAll();
