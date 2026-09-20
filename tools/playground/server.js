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
    // Kept so a browser refresh can restore the "Session Details" panel
    // (R_HOME/PYTHONHOME etc) for a reconnected session -- see
    // loadExistingSessions() in public/index.html. This server process
    // already knows this at creation time (it's right here in `options`),
    // but a page reload wipes the *browser's* own copy of it (that page's
    // whole `sessions` map is pure in-memory client state); without saving
    // it here too, GET /api/sessions had no way to hand it back, and every
    // reconnected session showed a permanently blank R_HOME/PYTHONHOME
    // until the whole session was torn down and recreated.
    entry.config = entry.kernelType === 'python'
        ? { pythonHome: options.pythonHome, pythonPath: options.pythonPath, venvPath: options.venvPath }
        : { rHome: options.rHome, rPath: options.rPath, rLibs: options.rLibs };

    // Full transcript (code + every iopub message it produced), kept here
    // for the same reason `entry.config` is: this SERVER process doesn't
    // get wiped by a browser refresh the way the page's own in-memory
    // `sessions` map does, so it's the only place that can hand a
    // reconnecting browser back what it already ran -- see
    // loadExistingSessions()/hydrateSessionHistory() in public/index.html.
    // Without this, a reconnected session had its live status/PID/config
    // restored (all genuinely still true), but its entire visible output
    // panel came back empty even though nothing about the actual kernel or
    // its state had changed -- confirmed directly as a real, confusing gap
    // once the config-restoration fix above made every OTHER "reconnect
    // fully" expectation seem like it should already hold too.
    entry.history = [];
    entry.historyByMsgId = new Map();

    // Not also forwarding session.on('stdout', ...): StreamHandler
    // (lib/handlers/stream-handler.ts) emits it from the exact same
    // 'stream' iopub message this 'message' listener already gets --
    // rendering both would duplicate every print()/cat() line.
    session.on('message', (message) => {
        broadcast(id, { event: 'message', message });
        recordHistory(entry, message);
    });
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

// Local dev tool, not a real notebook store -- bounded so a long-running
// session streaming forever (e.g. the "Streaming loop" preset left running)
// can't grow this without limit; old cells are dropped, newest kept.
const MAX_HISTORY_CELLS = 200;

// Buckets every iopub message this session produces by which execute_request
// (parentMsgId) it belongs to, purely from the messages themselves --
// execute_input's own content.code/execution_count is enough to start a new
// cell, so this needs no separate bookkeeping of the original REST request.
function recordHistory(entry, message) {
    if (message.msgType === 'execute_input') {
        const cell = {
            parentMsgId: message.parentMsgId,
            executionCount: message.content?.execution_count,
            code: message.content?.code ?? '',
            time: Date.now(),
            messages: []
        };
        entry.history.push(cell);
        entry.historyByMsgId.set(message.parentMsgId, cell);
        if (entry.history.length > MAX_HISTORY_CELLS) {
            const removed = entry.history.shift();
            entry.historyByMsgId.delete(removed.parentMsgId);
        }
        return;
    }
    // A stale input_request makes no sense to replay after a reconnect --
    // by the time anyone reconnects, it's either long since been answered
    // (over the stdin channel, which never shows up as a 'message' event at
    // all -- see Session's own input_request/sendInputReply handling) or
    // whatever was blocked on it is long gone from this browser's
    // perspective either way. Replaying it would render a "live",
    // answerable input box for a request nothing is actually still waiting
    // on.
    if (message.msgType === 'input_request') {
        return;
    }
    const cell = entry.historyByMsgId.get(message.parentMsgId);
    if (cell) {
        cell.messages.push(message);
    }
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
                sessions: [...sessions.entries()].map(([id, e]) => ({ id, status: e.status, kernelType: e.kernelType, config: e.config }))
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

            // GET /api/sessions/:id/info -- proxies the supervisor's own
            // GET {httpBase}/sessions/{sessionId}, which is the only place
            // pid/memoryBytes (native/src/themisto/session_registry.cpp's
            // sessionToJson()) actually live; this server's own in-memory
            // `sessions` map only tracks what it needs for SSE routing.
            if (req.method === 'GET' && parts[3] === 'info') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                try {
                    const upstream = await fetch(`${entry.session.info.httpBase}/sessions/${entry.session.info.sessionId}`);
                    const data = await upstream.json();
                    sendJson(res, upstream.status, data);
                } catch (error) {
                    sendJson(res, 502, { error: String(error?.message ?? error) });
                }
                return;
            }

            // GET /api/sessions/:id/history -- the full transcript recorded
            // so far (see recordHistory()), for a reconnecting browser to
            // replay through its own existing handleStreamEvent() and
            // rebuild the visible console exactly as it would have rendered
            // live.
            if (req.method === 'GET' && parts[3] === 'history') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                sendJson(res, 200, { history: entry.history });
                return;
            }

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
                        timeout: typeof body.timeout === 'number' ? body.timeout : undefined,
                        // The playground always has UI to answer an
                        // input_request (see the frontend's 'input_request'
                        // handling and the /input endpoint below), so it
                        // opts every execution into interactive input by
                        // default rather than making every preset/call site
                        // remember to ask for it.
                        allowStdin: body.allowStdin !== false,
                        // silent=true suppresses the kernel's own
                        // execute_input publish (and its execution-count
                        // increment/history-manager storage) -- used by the
                        // frontend's hidden version-probe cell so it doesn't
                        // consume a real "In [N]" number or get recorded
                        // into this server's own session history (see
                        // recordHistory() below, which anchors a history
                        // cell on execute_input specifically). Real output
                        // (stream messages) still comes through either way;
                        // only silent executions skip publishing this.
                        silent: body.silent === true
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

            // POST /api/sessions/:id/input -- answers a pending input_request
            // (see Session.sendInputReply()); fire-and-forget, same as
            // /interrupt below, since the reply that matters here is
            // whatever the blocked execute() call eventually resolves with,
            // not a reply to this call itself.
            if (req.method === 'POST' && parts[3] === 'input') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                const body = await readJsonBody(req);
                entry.session.sendInputReply(String(body.value ?? ''));
                sendJson(res, 200, { ok: true });
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

            // POST /api/sessions/:id/kill -- fire-and-forget stop(), not
            // Session.kill() alone. kill() only closes this process's own
            // WebSocket and clears its local queue; it never asks the
            // supervisor to actually terminate the underlying kernel
            // process. Confirmed directly: after calling it, the real
            // carpo.exe/elara.exe process kept running, orphaned,
            // indefinitely -- Session.kill() was designed for whole-app
            // teardown (where SessionManager.killAll() also force-kills the
            // supervisor process itself moments later, taking every child
            // with it), not as a safe per-session action, which is exactly
            // how this "Kill" button uses it. stop() actually terminates
            // the process (graceful shutdown_request first, with the
            // native side's own ~2.3s force-kill fallback if it doesn't
            // exit cleanly) via the supervisor; not awaiting it here keeps
            // this endpoint's perceived speed the same as before.
            if (req.method === 'POST' && parts[3] === 'kill') {
                if (!entry) {
                    sendJson(res, 404, { error: 'unknown session' });
                    return;
                }
                entry.session.stop().catch(() => {});
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
