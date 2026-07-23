import { SessionManager } from './dist/lib/index.js';

// Each session now runs in its own OS process (see lib/session/session-manager.ts),
// so Session 1's Shiny app blocking its R interpreter can no longer starve
// Session 2's execute() calls -- they're different embedded R interpreters
// entirely, not two objects sharing one. Ports are still auto-assigned per
// session (find_free_port() in native/src/transport/common/middleware.cpp),
// so nothing here collides regardless of how many sessions run at once.
const manager = new SessionManager();

const rOptions = {
    rHome: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r',
    rPath: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/bin/x64/',
    rLibs: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/library'
    // Note: a `logger` callback can't cross the process boundary to a
    // session's child process -- use the 'message'/'stdout' events below instead.
};

async function runSession1(session) {
    session.on('stdout', (text) => process.stdout.write(`[Session 1 Print] ${text}`));
    session.on('message', (msg) => {
        if (msg.msgType === 'error') {
            console.error('\n=== Session 1 R ERROR DETAILS ===');
            console.error('Error Name:', msg.content.ename);
            console.error('Error Value:', msg.content.evalue);
            console.error('Traceback:', msg.content.traceback?.join('\n'));
            console.error('======================\n');
        }
    });

    console.log('\n=== Session 1 Ready ===');
    console.log('Launching Shiny app via createShiny()...\n');

    const appPath = 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/shiny/rmncah';

    try {
        const handle = await session.createShiny({
            appDir: appPath,
            env: {
                CDSUITE_SHINY_ID: 'rmncah',
                CDSUITE_SHINY_NAME: 'rmncah',
                CDSUITE_SHINY_VERSION: '1.0.0',
                CDSUITE_SHINY_SELECTED_FILE: 'C:/Users/Murage/Downloads/Uganda_202605220630.rds',
                CDSUITE_SHINY_LOCALE: 'en'
            }
        });
        console.log(`[Session 1] Shiny app is live at ${handle.url}`);

        handle.done.then((result) => {
            console.log('[Session 1] Shiny app stopped:', result.success ? 'ok' : 'error');
        });
    } catch (err) {
        console.error('[Session 1] Failed to launch Shiny app:', err);
    }
}

async function runSession2(session) {
    session.on('stdout', (text) => process.stdout.write(`[Session 2 Print] ${text}`));

    console.log('\n=== Session 2 Ready ===');
    console.log('Sending simple R code -- this now runs concurrently with Session 1\'s Shiny app instead of timing out behind it.\n');

    const helloResult = await session.execute('print("Hello from Session 2!")');
    console.log('[Session 2] print() result:', helloResult.success ? 'ok' : 'error');

    const mathResult = await session.execute('1 + 1');
    console.log('[Session 2] 1 + 1 result:', mathResult.success ? 'ok' : 'error');
}

// A session actively running a blocking call (Session 1's Shiny app, via
// shiny::runApp()) can't process a graceful shutdown_request until that
// call returns -- its R interpreter thread is the same thread that would
// need to notice the request. So Ctrl+C tries a graceful stopAll() first,
// but falls back to forcibly killing every session's process after a
// timeout, so exiting is never blocked on a session that's busy.
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
