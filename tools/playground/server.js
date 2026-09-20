#!/usr/bin/env node
// Visual playground for exercising this package's real public API
// (SessionManager/Session from dist/lib/index.js -- the exact same module
// index.js itself uses) through a browser instead of hand-editing a script.
// Each "session" here is a real R process spawned by themisto,
// not a mock -- Run/Stop/Kill and the preset buttons drive the genuine
// execute()/stop()/kill() calls and surface their real events (stdout,
// structured iopub messages, kernel crashes) live over Server-Sent Events.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { execSync } from 'node:child_process';

import { SessionManager } from '../../dist/lib/index.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PLAYGROUND_PORT) || 4173;

const manager = new SessionManager();

/** @type {Map<string, { session: import('../../dist/lib/index.js').Session, clients: Set<import('node:http').ServerResponse>, status: string, kernelType?: string }>} */
const sessions = new Map();

// R_HOME isn't set as an inherited env var by every R install (confirmed
// via a real CI failure once this fell through to a hardcoded Windows-only
// path on Linux/macOS). `R RHOME` is R's own portable way of answering
// this on every platform, matching cmake/FindR.cmake's own technique; the
// hardcoded path remains only as a last resort for a Windows machine with
// R installed but not on PATH at all.
function discoverRHome() {
    if (process.env.R_HOME) return process.env.R_HOME;
    try {
        return execSync('R RHOME', { encoding: 'utf8' }).trim();
    } catch {
        return 'C:/Program Files/R/R-4.6.0';
    }
}

function defaultREnv() {
    // Same fallback pattern test/integration/session-manager.test.ts uses --
    // this machine's R install, overridable via R_HOME/R_PATH/R_LIBS so the
    // tool isn't hardcoded to one developer's filesystem.
    const rHome = discoverRHome();
    return {
        rHome,
        rPath: process.env.R_PATH || `${rHome}/bin/x64`,
        rLibs: process.env.R_LIBS || ''
    };
}

// Same "ask the runtime itself" pattern as discoverRHome() above (and
// tools/jupyter-kernelspec/generate.js's own copy of this) -- sys.prefix is
// the portable, correct PYTHONHOME for whichever Python is actually found.
function discoverPythonHome() {
    if (process.env.PYTHONHOME) return process.env.PYTHONHOME;
    for (const cmd of ['python3', 'python']) {
        try {
            return execSync(`${cmd} -c "import sys; print(sys.prefix)"`, { encoding: 'utf8' }).trim();
        } catch {
            // Try the next candidate command name.
        }
    }
    return '';
}

function defaultPythonEnv() {
    return {
        pythonHome: discoverPythonHome()
    };
}

function broadcast(id, payload) {
    const entry = sessions.get(id);
    if (!entry) return;
    const data = `data: ${JSON.stringify(payload)}\n\n`;
    for (const res of entry.clients) {
        res.write(data);
    }
}

async function createSession(options) {
    // Generated up front (not after the session comes back) because the
    // logger callback below can fire while manager.createSession() is still
    // in flight (e.g. Session's own "Connecting to session X at ws://..."
    // trace log) -- it needs `id` closed over and ready before that happens.
    // supervisor-client.ts assigns its own session id internally, but
    // nothing on the public Session API exposes it, so this tool keys its
    // registry on a separately-generated one instead.
    const id = randomUUID();
    const entry = { session: undefined, clients: new Set(), status: 'starting' };
    sessions.set(id, entry);

    let session;
    try {
        session = await manager.createSession({
            kernelType: options.kernelType,
            rHome: options.rHome,
            rPath: options.rPath || undefined,
            rLibs: options.rLibs || undefined,
            pythonHome: options.pythonHome || undefined,
            pythonPath: options.pythonPath || undefined,
            venvPath: options.venvPath || undefined,
            enableLogging: true,
            logger: (level, message, data) => {
                broadcast(id, { event: 'log', level, message, data });
            }
        });
    } catch (error) {
        sessions.delete(id);
        throw error;
    }

    entry.session = session;
    entry.status = 'ready';
    entry.kernelType = options.kernelType || 'r';

    // Not also forwarding session.on('stdout', ...): StreamHandler
    // (lib/handlers/stream-handler.ts) emits it from the exact same
    // 'stream' iopub message this 'message' listener already gets --
    // rendering both would duplicate every print()/cat() line.
    session.on('message', (message) => broadcast(id, { event: 'message', message }));
    session.on('exit', (info) => {
        entry.status = 'crashed';
        broadcast(id, { event: 'exit', reason: info?.reason });
    });
    session.on('stopped', () => {
        entry.status = 'stopped';
        broadcast(id, { event: 'stopped' });
    });
    session.on('restarted', () => {
        entry.status = 'ready';
        broadcast(id, { event: 'restarted' });
    });
    // EventEmitter's 'error' is special-cased by Node -- with zero listeners
    // it throws instead of emitting, crashing this process. This listener
    // must stay registered even though most of what lands here is noise:
    // ErrorHandler (lib/handlers/error-handler.ts) re-emits every R error's
    // evalue (a plain string) under this same event name, and that's
    // already fully represented -- with a traceback -- in the 'message'
    // event above. Only forward genuine Error instances (a real
    // connection/protocol problem, from Session's own handleFrame catch
    // block) as a distinct SSE event; string-typed ones are dropped here to
    // avoid rendering every R stop() twice.
    session.on('error', (error) => {
        if (error instanceof Error) {
            broadcast(id, { event: 'connectionError', message: error.message });
        }
    });

    return id;
}

function sendJson(res, status, body) {
    const text = JSON.stringify(body);
    res.writeHead(status, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(text)
    });
    res.end(text);
}

async function readJsonBody(req) {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    if (chunks.length === 0) return {};
    try {
        return JSON.parse(Buffer.concat(chunks).toString('utf-8'));
    } catch {
        return {};
    }
}

const server = createServer(async (req, res) => {
    const url = new URL(req.url, `http://${req.headers.host}`);
    const parts = url.pathname.split('/').filter(Boolean);

    try {
        // GET /
        if (req.method === 'GET' && parts.length === 0) {
            const html = await readFile(path.join(__dirname, 'public', 'index.html'), 'utf-8');
            res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
            res.end(html);
            return;
        }

        // GET /api/defaults
        if (req.method === 'GET' && url.pathname === '/api/defaults') {
            sendJson(res, 200, { ...defaultREnv(), ...defaultPythonEnv() });
            return;
        }

        // GET /api/sessions
        if (req.method === 'GET' && url.pathname === '/api/sessions') {
            sendJson(res, 200, {
                sessions: [...sessions.entries()].map(([id, e]) => ({ id, status: e.status, kernelType: e.kernelType }))
            });
            return;
        }

        // POST /api/sessions
        if (req.method === 'POST' && url.pathname === '/api/sessions') {
            const body = await readJsonBody(req);
            const kernelType = body.kernelType === 'python' ? 'python' : 'r';
            const rDefaults = defaultREnv();
            const pyDefaults = defaultPythonEnv();
            try {
                const id = await createSession({
                    kernelType,
                    rHome: body.rHome || rDefaults.rHome,
                    rPath: body.rPath || rDefaults.rPath,
                    rLibs: body.rLibs || rDefaults.rLibs,
                    pythonHome: body.pythonHome || pyDefaults.pythonHome,
                    pythonPath: body.pythonPath,
                    venvPath: body.venvPath
                });
                sendJson(res, 201, { id });
            } catch (error) {
                sendJson(res, 500, { error: String(error?.message ?? error) });
            }
            return;
        }

        // Everything else is scoped to /api/sessions/:id/...
        if (parts[0] === 'api' && parts[1] === 'sessions' && parts[2]) {
            const id = parts[2];
            const entry = sessions.get(id);

            // GET /api/sessions/:id/stream  (SSE)
            if (req.method === 'GET' && parts[3] === 'stream') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                res.writeHead(200, {
                    'Content-Type': 'text/event-stream',
                    'Cache-Control': 'no-cache',
                    Connection: 'keep-alive'
                });
                res.write(`data: ${JSON.stringify({ event: 'connected', status: entry.status })}\n\n`);
                entry.clients.add(res);

                const heartbeat = setInterval(() => res.write(': ping\n\n'), 15000);
                req.on('close', () => {
                    clearInterval(heartbeat);
                    entry.clients.delete(res);
                });
                return;
            }

            // POST /api/sessions/:id/execute
            if (req.method === 'POST' && parts[3] === 'execute') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                const body = await readJsonBody(req);
                try {
                    const result = await entry.session.execute(body.code ?? '', {
                        timeout: typeof body.timeout === 'number' ? body.timeout : undefined
                    });
                    sendJson(res, 200, {
                        ok: true,
                        success: result.success,
                        executionCount: result.executionCount,
                        error: result.error ? String(result.error.message ?? result.error) : undefined
                    });
                } catch (error) {
                    sendJson(res, 200, { ok: false, success: false, error: String(error?.message ?? error) });
                }
                return;
            }

            // POST /api/sessions/:id/interrupt
            if (req.method === 'POST' && parts[3] === 'interrupt') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                entry.session.interrupt();
                sendJson(res, 200, { ok: true });
                return;
            }

            // POST /api/sessions/:id/restart
            if (req.method === 'POST' && parts[3] === 'restart') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                try {
                    await entry.session.restart();
                    sendJson(res, 200, { ok: true });
                } catch (error) {
                    sendJson(res, 500, { ok: false, error: String(error?.message ?? error) });
                }
                return;
            }

            // POST /api/sessions/:id/stop
            if (req.method === 'POST' && parts[3] === 'stop') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                await entry.session.stop();
                sendJson(res, 200, { ok: true });
                return;
            }

            // POST /api/sessions/:id/kill
            if (req.method === 'POST' && parts[3] === 'kill') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                entry.session.kill();
                entry.status = 'stopped';
                broadcast(id, { event: 'stopped' });
                sendJson(res, 200, { ok: true });
                return;
            }
        }

        sendJson(res, 404, { error: 'not found' });
    } catch (error) {
        sendJson(res, 500, { error: String(error?.message ?? error) });
    }
});

server.listen(PORT, () => {
    console.log(`\nJovian playground running at http://127.0.0.1:${PORT}\n`);
    console.log(`R install: ${JSON.stringify(defaultREnv(), null, 2)}`);
    console.log(`Python install: ${JSON.stringify(defaultPythonEnv(), null, 2)}\n`);
    console.log('Press Ctrl+C to stop.\n');
});

let shuttingDown = false;
process.on('SIGINT', async () => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log('\nShutting down playground...');

    const timeout = setTimeout(() => {
        manager.killAll();
        process.exit(0);
    }, 5000);

    await manager.stopAll();
    clearTimeout(timeout);
    process.exit(0);
});
