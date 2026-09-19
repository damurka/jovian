#!/usr/bin/env node
// Terminal-only sibling to tools/playground/server.js -- same real
// SessionManager/Session API index.js uses, same preset/crash/restart
// scenarios as the browser playground, but a single Node process talking
// directly to stdin/stdout instead of spinning up an HTTP server + browser.
// Plots can't render in a terminal, so display_data image/png gets saved to
// a temp file and opened in the OS's default viewer instead.
import { createInterface } from 'node:readline';
import { writeFile, mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { exec } from 'node:child_process';

import { SessionManager } from '../../dist/lib/index.js';

const col = {
    reset: '\x1b[0m', dim: '\x1b[2m', bold: '\x1b[1m',
    red: '\x1b[31m', green: '\x1b[32m', yellow: '\x1b[33m', cyan: '\x1b[36m'
};
const paint = (color, text) => `${col[color]}${text}${col.reset}`;

// { code } or { code, timeout } -- same scenarios as tools/playground/public/index.html's PRESETS.
const PRESETS = {
    hello: { code: `print("Hello from the playground!")` },
    math: { code: `sum(1:100)` },
    table: { code: `head(mtcars)` },
    plot: { code: `plot(1:10, main = "Playground Plot")` },
    stream: { code: `for (i in 1:5) {\n  cat("tick", i, "\\n")\n  Sys.sleep(0.4)\n}` },
    warn: { code: `warning("This is a simulated warning")` },
    error: { code: `stop("Simulated error from the playground")` },
    runtimeerror: { code: `nonexistent_variable_xyz + 1` },
    // Detection is heartbeat-based (ClientHeartbeat, hardcoded 20s x 3
    // retries in ClientZmqImpl) -- genuinely takes ~60-80s, not a hang.
    crash: { code: `quit(save = "no")` },
    timeout: { code: `Sys.sleep(30)`, timeout: 3000 }
};

function defaultREnv() {
    const rHome = process.env.R_HOME || 'C:/Program Files/R/R-4.6.0';
    return {
        rHome,
        rPath: process.env.R_PATH || `${rHome}/bin/x64`,
        rLibs: process.env.R_LIBS || ''
    };
}

const manager = new SessionManager();
/** @type {import('../../dist/lib/index.js').Session | null} */
let session = null;
let plotCounter = 0;
const plotsDir = await mkdtemp(join(tmpdir(), 'jovian-playground-'));

function openFile(filePath) {
    const cmd = process.platform === 'win32' ? `start "" "${filePath}"`
        : process.platform === 'darwin' ? `open "${filePath}"`
        : `xdg-open "${filePath}"`;
    exec(cmd, () => { /* best-effort */ });
}

async function savePlot(pngData) {
    plotCounter += 1;
    const base64 = String(pngData).startsWith('data:') ? String(pngData).split(',')[1] : pngData;
    const filePath = join(plotsDir, `plot-${plotCounter}.png`);
    await writeFile(filePath, Buffer.from(base64, 'base64'));
    console.log(paint('cyan', `[plot saved to ${filePath}, opening...]`));
    openFile(filePath);
}

function printMessage(msg) {
    switch (msg.msgType) {
        case 'stream': {
            const isErr = msg.content?.name === 'stderr';
            const text = msg.content?.text ?? '';
            process.stdout.write(isErr ? paint('yellow', text) : text);
            break;
        }
        case 'execute_result':
        case 'display_data': {
            const data = msg.content?.data;
            if (!data) break;
            if (data['image/png']) {
                void savePlot(data['image/png']);
            } else if (data['text/plain'] !== undefined) {
                const v = data['text/plain'];
                console.log(paint('green', Array.isArray(v) ? v.join('\n') : String(v)));
            }
            break;
        }
        case 'error': {
            const ct = msg.content ?? {};
            console.log(paint('red', `${col.bold}${ct.ename ?? 'Error'}${col.reset}${col.red}: ${ct.evalue ?? ''}`));
            if (Array.isArray(ct.traceback)) {
                console.log(paint('dim', ct.traceback.join('\n')));
            }
            break;
        }
        default:
            break;
    }
}

async function createSession() {
    const defaults = defaultREnv();
    console.log(paint('dim', `Spawning R session (R_HOME=${defaults.rHome})...`));
    session = await manager.createSession({
        rHome: defaults.rHome,
        rPath: defaults.rPath,
        rLibs: defaults.rLibs || undefined,
        // Not enableLogging: true -- LoggingMiddleware (lib/middleware/
        // plugins/logging-plugin.ts) dumps every raw WS frame straight to
        // console.log, which would fight with this REPL's own formatted
        // rendering below. The logger callback alone still gets us
        // warn/error-level diagnostics.
        logger: (level, message) => {
            if (level === 'error' || level === 'warn') {
                console.log(paint('dim', `[${level}] ${message}`));
            }
        }
    });
    session.on('message', printMessage);
    // EventEmitter's 'error' is special-cased by Node -- with zero
    // listeners it throws instead of emitting, which would otherwise crash
    // this whole process the first time an R stop()/error occurred (see
    // tools/playground/server.js's identical guard, which this mirrors).
    // ErrorHandler re-emits every R error's evalue as a plain string under
    // this same event name; that's already shown -- with a traceback -- via
    // printMessage() above, so only a genuine Error instance (a real
    // connection/protocol problem) gets a second, distinct line here.
    session.on('error', (error) => {
        if (error instanceof Error) {
            console.log(paint('red', `Connection error: ${error.message}`));
        }
    });
    session.on('exit', (info) => {
        const reason = info?.reason ? `: ${info.reason}` : '';
        console.log(paint('red', `\n✗ Kernel process exited unexpectedly${reason}. Use /restart to recover, or /new for a fresh session.`));
        showPrompt();
    });
    session.on('stopped', () => {
        console.log(paint('dim', '\nSession stopped.'));
    });
    session.on('restarted', () => {
        console.log(paint('green', '\n✓ Session restarted -- fresh kernel, same session id.'));
    });
    console.log(paint('green', '✓ Session ready. Type R code and press Enter, or /help for commands.'));
}

async function runCode(code, timeout) {
    if (!session) {
        console.log(paint('red', 'No active session -- try /new.'));
        return;
    }
    try {
        const result = await session.execute(code, { timeout });
        if (result.success) {
            console.log(paint('dim', `✓ done${typeof result.executionCount === 'number' ? ' [' + result.executionCount + ']' : ''}`));
        } else {
            console.log(paint('red', `✗ failed${result.error ? ': ' + result.error.message : ''}`));
        }
    } catch (err) {
        console.log(paint('red', `Request failed: ${err.message}`));
    }
}

function printHelp() {
    console.log(`
${paint('bold', 'Commands')}
  /help              show this help
  /presets           list preset scenarios
  /run <name>        run a preset (e.g. /run error)
  /restart           restart the session in place (recovers a crash too)
  /new               abandon this session and spawn a brand new one
  /status            show whether a session is active
  /stop               gracefully stop the session and exit
  /kill              force-close the session and exit
  /exit, /quit       same as /kill

${paint('bold', 'R code')}
  Type R code and press Enter to run it. Multi-line input (unbalanced
  (){}[] or an open quote) keeps prompting with '+' until it balances --
  same as R's own console.
`);
}

function printPresets() {
    console.log(`${paint('bold', 'Presets')} (run with /run <name>):`);
    for (const [name, { code, timeout }] of Object.entries(PRESETS)) {
        const firstLine = code.split('\n')[0];
        console.log(`  ${paint('cyan', name.padEnd(14))} ${firstLine}${code.includes('\n') ? ' …' : ''}${timeout ? paint('dim', ` (timeout ${timeout}ms)`) : ''}`);
    }
}

async function handleCommand(cmd) {
    const [name, ...rest] = cmd.slice(1).trim().split(/\s+/);
    switch (name) {
        case 'help':
            printHelp();
            break;
        case 'presets':
            printPresets();
            break;
        case 'run': {
            const preset = PRESETS[rest[0]];
            if (!preset) {
                console.log(paint('red', `Unknown preset "${rest[0] ?? ''}". Try /presets.`));
                break;
            }
            console.log(paint('dim', `> ${preset.code}`));
            await runCode(preset.code, preset.timeout);
            break;
        }
        case 'restart':
            if (!session) {
                console.log(paint('red', 'No active session.'));
                break;
            }
            console.log(paint('dim', 'Restarting (spawns a fresh kernel under the same session)...'));
            try {
                await session.restart();
            } catch (err) {
                console.log(paint('red', `Restart failed: ${err.message}`));
            }
            break;
        case 'new':
            await createSession();
            break;
        case 'status':
            console.log(paint('dim', session ? 'A session is active.' : 'No active session.'));
            break;
        case 'stop':
            if (session) await session.stop();
            rl.close();
            process.exit(0);
            break;
        case 'kill':
        case 'exit':
        case 'quit':
            if (session) session.kill();
            rl.close();
            process.exit(0);
            break;
        default:
            console.log(paint('red', `Unknown command: /${name}. Type /help.`));
    }
}

// Minimal R-shaped balance check (parens/braces/brackets + quotes, '#'
// comments) -- not a real parser, just enough to let multi-line snippets
// like a for-loop be typed naturally, prompting with '+' until they close.
function isBalanced(code) {
    let depth = 0;
    let inString = null;
    for (let i = 0; i < code.length; i++) {
        const ch = code[i];
        if (inString) {
            if (ch === '\\') { i++; continue; }
            if (ch === inString) inString = null;
            continue;
        }
        if (ch === '"' || ch === "'") { inString = ch; continue; }
        if (ch === '#') {
            const nl = code.indexOf('\n', i);
            if (nl === -1) break;
            i = nl;
            continue;
        }
        if (ch === '(' || ch === '{' || ch === '[') depth++;
        else if (ch === ')' || ch === '}' || ch === ']') depth--;
    }
    return depth <= 0 && inString === null;
}

console.log(paint('bold', '\njovian playground (terminal)\n'));

await createSession();

let buffer = '';
const rl = createInterface({ input: process.stdin, output: process.stdout });

function showPrompt() {
    // For piped/file input, EOF (and the interface's internal "closed"
    // state) can be reached before the `chain` promise below has drained
    // every queued line -- e.g. an early execute() still in flight when the
    // input file ends. A still-pending chain link calling this afterward
    // would otherwise throw ("readline was closed") instead of just being
    // a no-op, which is all a prompt redraw after the interface is gone
    // should be. Doesn't affect real interactive use, where stdin only
    // closes once the user (or /kill's own rl.close()) is done.
    try {
        rl.setPrompt(paint('cyan', buffer ? '+ ' : '> '));
        rl.prompt();
    } catch {
        // interface already closed -- nothing to redraw
    }
}

async function processLine(line) {
    if (!buffer && line.trim().startsWith('/')) {
        await handleCommand(line.trim());
        showPrompt();
        return;
    }

    buffer += (buffer ? '\n' : '') + line;
    if (buffer.trim() === '' || isBalanced(buffer)) {
        const code = buffer;
        buffer = '';
        if (code.trim() !== '') {
            await runCode(code, undefined);
        }
    }
    showPrompt();
}

// readline's 'line' event fires for every buffered line back-to-back
// (piped input, or a human pasting a multi-line snippet) without waiting
// for an async listener to finish -- an `async (line) => {...}` handler
// here would let a later line's work (including /kill's process.exit(0))
// race ahead of an earlier line's still-in-flight execute() call. Chaining
// through one promise serializes them so each line's output/side effects
// are guaranteed to land before the next line starts.
let chain = Promise.resolve();
rl.on('line', (line) => {
    chain = chain.then(() => processLine(line)).catch((err) => {
        console.error(paint('red', `Unexpected error: ${err.message}`));
    });
});

rl.on('close', async () => {
    // For piped/file input (vs. a real interactive terminal), EOF arrives
    // essentially right after the last line is read -- well before the
    // `chain` promise (every queued line's async work, including any
    // still-in-flight execute()) has actually finished. Exiting
    // unconditionally here raced ahead of it the same way the unserialized
    // 'line' handler used to.
    await chain.catch(() => {});
    process.exit(0);
});

let shuttingDown = false;
process.on('SIGINT', async () => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(paint('dim', '\nShutting down...'));

    const timeout = setTimeout(() => {
        manager.killAll();
        process.exit(0);
    }, 5000);

    await manager.stopAll();
    clearTimeout(timeout);
    process.exit(0);
});

showPrompt();
