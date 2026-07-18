import { DatasuiteEngine } from './dist/lib/index.js';

// TEST: Pass explicit environment configuration to the engine
const engine = new DatasuiteEngine({
    // Use system R installation which has all dependencies
    rHome: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r',
    rPath: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/bin/x64/',
    // Don't set rLibs - let R use its default paths (includes user library where hera is installed)
    // rLibs: 'C:/Program Files/R/R-4.6.0/library',
    rLibs: 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/win32/r/library',
    // 2. Use the new logger to verify the boot sequence
    logger: (level, msg) => {
        // We will color-code the logs to easily distinguish them from R outputs
        console.log(`[Engine Boot Log -> ${level.toUpperCase()}]: ${msg}`);
    }
});

// 1. Wire up your standard listeners
engine.on('stdout', (text) => process.stdout.write(`[Print] ${text}`));
engine.on('result', (res) => console.log(`[Result] ${res}`));
engine.on('error', (err) => console.error(`[Fatal R Error] ${err}`));

// Capture detailed error information
engine.on('message', (msg) => {
    if (msg.topic.includes('error')) {
        console.error('\n=== R ERROR DETAILS ===');
        console.error('Error Name:', msg.content.ename);
        console.error('Error Value:', msg.content.evalue);
        console.error('Traceback:', msg.content.traceback?.join('\n'));
        console.error('======================\n');
    }
});

// (Optional) Test the new Catch-All wildcard event we just built!
engine.on('*', (topic, content) => {
    // Uncomment this if you want to see the raw ZeroMQ JSON payloads flowing through!
    console.log(`[Raw ZeroMQ Topic: ${topic}]`);
});

engine.on('ready', () => {
    console.log("\n=== Engine Fully Booted & Ready ===");
    console.log("Sending Shiny app to C++ background thread...\n");

    // 2. Execute R code!
    // Run the actual complex Shiny app from the directory
    // Use port = 0 to let R automatically pick an available port
    const appPath = 'c:/Users/Murage/Documents/Dev/JS/datasuite_old_with_ai/resources/shiny/rmncah';
    const complexApp = `shiny::runApp('${appPath}', launch.browser = FALSE)`;
    
    console.log(`Loading Shiny app from: ${appPath}`);
    engine.execute(complexApp);
});

// Boot the kernel
engine.start();