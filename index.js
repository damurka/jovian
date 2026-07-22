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

// Capture detailed error information
engine1.on('message', (msg) => {
    if (msg.topic.includes('error')) {
        console.error('\n=== Engine 1 R ERROR DETAILS ===');
        console.error('Error Name:', msg.content.ename);
        console.error('Error Value:', msg.content.evalue);
        console.error('Traceback:', msg.content.traceback?.join('\n'));
        console.error('======================\n');
    }
});

engine2.on('message', (msg) => {
    if (msg.topic.includes('error')) {
        console.error('\n=== Engine 2 R ERROR DETAILS ===');
        console.error('Error Name:', msg.content.ename);
        console.error('Error Value:', msg.content.evalue);
        console.error('Traceback:', msg.content.traceback?.join('\n'));
        console.error('======================\n');
    }
});

// (Optional) Test the new Catch-All wildcard event we just built!
engine1.on('*', (topic, content) => {
    // Uncomment this if you want to see the raw ZeroMQ JSON payloads flowing through!
    // console.log(`[Engine 1 Raw ZeroMQ Topic: ${topic}]`);
});

engine2.on('*', (topic, content) => {
    // Uncomment this if you want to see the raw ZeroMQ JSON payloads flowing through!
    // console.log(`[Engine 2 Raw ZeroMQ Topic: ${topic}]`);
});

engine1.on('ready', () => {
    console.log("\n=== Engine 1 Fully Booted & Ready ===");
    console.log("Sending Shiny app to C++ background thread...\n");

    // 2. Execute R code!
    // Run the actual complex Shiny app from the directory
    // Use port = 0 to let R automatically pick an available port
    const appPath = 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/shiny/rmncah';
    const complexApp = `shiny::runApp('${appPath}', launch.browser = FALSE)`;
    
    console.log(`Engine 1 Loading Shiny app from: ${appPath}`);
    engine1.execute(complexApp);
});

engine2.on('ready', () => {
    console.log("\n=== Engine 2 Fully Booted & Ready ===");
    console.log("Sending simple R code to C++ background thread...\n");

    engine2.execute('print("Hello from Engine 2!")');
    engine2.execute('1 + 1');
});

// Boot the kernels
engine1.start();
engine2.start();