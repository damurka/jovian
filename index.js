import { DatasuiteEngine } from './dist/lib/index.js';

// TEST: Pass explicit environment configuration to the engine
const engine1 = new DatasuiteEngine({
    // Use system R installation which has all dependencies
    rHome: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r',
    rPath: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/bin/x64/',
    // Don't set rLibs - let R use its default paths (includes user library where hera is installed)
    // rLibs: 'C:/Program Files/R/R-4.6.0/library',
    rLibs: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/library',
    // 2. Use the new logger to verify the boot sequence
    logger: (level, msg) => {
        // We will color-code the logs to easily distinguish them from R outputs
        console.log(`[Engine 1 Boot Log -> ${level.toUpperCase()}]: ${msg}`);
    }
});

const engine2 = new DatasuiteEngine({
    // Use system R installation which has all dependencies
    rHome: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r',
    rPath: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/bin/x64/',
    // Don't set rLibs - let R use its default paths (includes user library where hera is installed)
    // rLibs: 'C:/Program Files/R/R-4.6.0/library',
    rLibs: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/library',
    // 2. Use the new logger to verify the boot sequence
    logger: (level, msg) => {
        // We will color-code the logs to easily distinguish them from R outputs
        console.log(`[Engine 2 Boot Log -> ${level.toUpperCase()}]: ${msg}`);
    }
});

// 1. Wire up your standard listeners
engine1.on('stdout', (text) => process.stdout.write(`[Engine 1 Print] ${text}`));
engine1.on('result', (res) => console.log(`[Engine 1 Result] ${res}`));
engine1.on('error', (err) => console.error(`[Engine 1 Fatal R Error] ${err}`));

engine2.on('stdout', (text) => process.stdout.write(`[Engine 2 Print] ${text}`));
engine2.on('result', (res) => console.log(`[Engine 2 Result] ${res}`));
engine2.on('error', (err) => console.error(`[Engine 2 Fatal R Error] ${err}`));

// Capture detailed error information (msgType is the plain Jupyter
// msg_type, e.g. "error" — `topic` is a kernel-namespaced string like
// "kernel_core.<id>.error" and isn't useful for this kind of matching)
engine1.on('message', (msg) => {
    if (msg.msgType === 'error') {
        console.error('\n=== Engine 1 R ERROR DETAILS ===');
        console.error('Error Name:', msg.content.ename);
        console.error('Error Value:', msg.content.evalue);
        console.error('Traceback:', msg.content.traceback?.join('\n'));
        console.error('======================\n');
    }
});

engine2.on('message', (msg) => {
    if (msg.msgType === 'error') {
        console.error('\n=== Engine 2 R ERROR DETAILS ===');
        console.error('Error Name:', msg.content.ename);
        console.error('Error Value:', msg.content.evalue);
        console.error('Traceback:', msg.content.traceback?.join('\n'));
        console.error('======================\n');
    }
});

// (Optional) Test the new Catch-All wildcard event we just built!
engine1.on('*', (msgType, content) => {
    // Uncomment this if you want to see the raw ZeroMQ JSON payloads flowing through!
    // console.log(`[Engine 1 Raw ZeroMQ msg_type: ${msgType}]`);
});

engine2.on('*', (msgType, content) => {
    // Uncomment this if you want to see the raw ZeroMQ JSON payloads flowing through!
    // console.log(`[Engine 2 Raw ZeroMQ msg_type: ${msgType}]`);
});

engine1.on('ready', async () => {
    console.log("\n=== Engine 1 Fully Booted & Ready ===");
    console.log("Launching Shiny app via createShiny()...\n");

    const appPath = 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/shiny/rmncah';

    try {
        // Resolves once the app is actually accepting connections (port is
        // auto-assigned unless given explicitly). `handle.done` resolves
        // separately, once the app stops -- shiny::runApp() blocks the R
        // session for as long as it's running.
        const handle = await engine1.createShiny({ appDir: appPath });
        console.log(`[Engine 1] Shiny app is live at ${handle.url}`);

        handle.done.then((result) => {
            console.log('[Engine 1] Shiny app stopped:', result.success ? 'ok' : 'error');
        });
    } catch (err) {
        console.error('[Engine 1] Failed to launch Shiny app:', err);
    }
});

// engine2.on('ready', async () => {
//     console.log("\n=== Engine 2 Fully Booted & Ready ===");
//     console.log("Sending simple R code to C++ background thread...\n");

//     const helloResult = await engine2.execute('print("Hello from Engine 2!")');
//     console.log('[Engine 2] print() result:', helloResult);

//     const mathResult = await engine2.execute('1 + 1');
//     console.log('[Engine 2] 1 + 1 result:', mathResult);
// });

// Boot the kernels — each embeds its own R session in this process and now
// gets its own OS-assigned ZMQ ports (see find_free_port() in
// native/src/transport/common/middleware.cpp), so running both concurrently
// no longer collides on the old hardcoded 50011-50015 range.
engine1.start();
// engine2.start();