import { spawn, type ChildProcess } from 'child_process';
import { closeSync, mkdirSync, openSync, readFileSync, rmSync, writeFileSync } from 'fs';
import { createInterface } from 'readline';
import { dirname, join } from 'path';
import type { EngineOptions, SessionStatusInfo } from '../types/index.js';
import { Logger } from '../utils/logger.js';
import { ensureExecutable, locateNativeDirectory } from './native-paths.js';

/** Kernel output lines worth attaching to an error. */
const NOTABLE_OUTPUT = /error|fatal|warning|failed|cannot|not found|no such/i;
/** How far back describeKernelExit() looks for what a crashed kernel printed. */
const KERNEL_EXIT_OUTPUT_WINDOW_MS = 15_000;

export interface SessionConnectionInfo {
    sessionId: string;
    httpBase: string;
    wsBase: string;
    /** The supervisor's access token -- sent with every request (see supervisorHeaders()). */
    token: string;
}

/** A session as the supervisor lists it: its status plus the options it was started with. */
export type SupervisorSessionInfo = SessionStatusInfo & { options?: Partial<EngineOptions> };

interface CreateSessionResponse {
    sessionId?: string;
    status?: string;
    error?: string;
}

// Pulled out as its own pure function (rather than inlined in createSession/
// restartSession) so the exact shape sent to the supervisor's POST /sessions
// and .../restart bodies is unit-testable without mocking fetch() or
// spawning a real supervisor process. Undefined fields are dropped by
// JSON.stringify() (e.g. rHome for a 'python' session), so passing every
// field unconditionally is harmless -- session_registry.cpp's
// parseSessionOptions() only reads the ones matching kernelType anyway.
export function buildSessionOptionsBody(options: Partial<EngineOptions>): Record<string, unknown> {
    return {
        kernelType: options.kernelType,
        rHome: options.rHome,
        rPath: options.rPath,
        rLibs: options.rLibs,
        pandocPath: options.pandocPath,
        pythonHome: options.pythonHome,
        pythonPath: options.pythonPath,
        venvPath: options.venvPath,
        stataHome: options.stataHome,
        stataEdition: options.stataEdition,
        arkPath: options.arkPath,
        kernelArgv: options.kernelArgv,
        kernelEnv: options.kernelEnv,
        kernelInterruptMode: options.kernelInterruptMode,
        workingDirectory: options.workingDirectory
    };
}

/** Where a running supervisor listens, and the token its API requires. */
export interface SupervisorEndpoint {
    httpBase: string;
    wsBase: string;
    token: string;
    /** The supervisor's process id. */
    pid: number;
}

/** The HTTP headers every request to the supervisor carries. */
export function supervisorHeaders(token: string, extra: Record<string, string> = {}): Record<string, string> {
    return token ? { authorization: `Bearer ${token}`, ...extra } : extra;
}

/** A session's WebSocket URL, with the token (Node's WebSocket cannot send headers). */
export function sessionSocketUrl(info: SessionConnectionInfo): string {
    const base = `${info.wsBase}/sessions/${info.sessionId}/messages`;
    return info.token ? `${base}?token=${encodeURIComponent(info.token)}` : base;
}

/** See SessionManagerOptions.persistent. */
export interface PersistentSupervisorOptions {
    stateFile: string;
    idleShutdownMinutes: number;
}

/** Reads a state file written by a persistent SupervisorClient (undefined if there is none, or it is unreadable). */
export function readSupervisorState(stateFile: string): SupervisorEndpoint | undefined {
    try {
        const state = JSON.parse(readFileSync(stateFile, 'utf8')) as Partial<SupervisorEndpoint>;
        if (typeof state.httpBase === 'string' && typeof state.wsBase === 'string' && typeof state.token === 'string') {
            return { httpBase: state.httpBase, wsBase: state.wsBase, token: state.token, pid: Number(state.pid) || 0 };
        }
    } catch {
        // No supervisor recorded, or the file is damaged: start a new one.
    }
    return undefined;
}

/** Where a persistent supervisor writes its output (the kernels' start-up lines): beside the state file. */
export function supervisorLogFile(stateFile: string): string {
    return stateFile.replace(/(\.json)?$/, '.log');
}

/** Writes the state file, readable by this user only (it holds the token). */
export function writeSupervisorState(stateFile: string, endpoint: SupervisorEndpoint): void {
    mkdirSync(dirname(stateFile), { recursive: true });
    writeFileSync(stateFile, JSON.stringify(endpoint), { mode: 0o600 });
}

// themisto is the kernel supervisor: it's the only process in this system
// that ever links a native ZMQ
// binding. It spawns/owns `elara` kernel processes, speaks ZMQ to
// each of them, and re-exposes sessions over plain HTTP (lifecycle) +
// WebSocket (execute/interrupt/message streaming) -- so Electron/VS Code's
// process, where this class runs, never needs a native dependency at all.
//
// Every request carries the token the supervisor printed in its ready line
// (native/src/themisto/access.hpp): the API is reachable by any program on
// this machine, and it runs code.
//
// Persistent mode (SessionManagerOptions.persistent): the supervisor is
// started detached, outlives this process, and is found again through a
// state file by the next SessionManager with the same stateFile -- so a
// window reload (Electron, VS Code) does not lose running sessions. It stops
// itself after idleShutdownMinutes without any client.
export class SupervisorClient {
    private child: ChildProcess | undefined;
    private readyPromise: Promise<SupervisorEndpoint> | undefined;
    private readonly logger: Logger;
    private readonly forwardKernelOutput: boolean;
    private readonly persistent: PersistentSupervisorOptions | undefined;
    // The supervisor's stderr is where every kernel's start-up output ends up
    // ([elara] ..., [carpo] ...). It is kept, not printed, unless asked for,
    // and attached to the error when a kernel fails to start or exits
    // unexpectedly.
    private readonly recentOutput: { text: string; at: number }[] = [];

    constructor(logger: Logger, options: { forwardKernelOutput?: boolean; persistent?: PersistentSupervisorOptions | undefined } = {}) {
        this.logger = logger;
        this.forwardKernelOutput = options.forwardKernelOutput ?? false;
        this.persistent = options.persistent;
    }

    /** Whether the supervisor is meant to outlive this process. */
    get isPersistent(): boolean {
        return this.persistent !== undefined;
    }

    private rememberOutput(line: string): void {
        if (!line.trim()) return;
        this.recentOutput.push({ text: line, at: Date.now() });
        if (this.recentOutput.length > 200) this.recentOutput.shift();
    }

    /** The message plus what the kernels said just before, when that was not already printed. */
    private withKernelOutput(message: string): string {
        if (this.forwardKernelOutput || this.recentOutput.length === 0) return message;
        const all = this.recentOutput.map((line) => line.text);
        const notable = all.filter((line) => NOTABLE_OUTPUT.test(line));
        const lines = (notable.length > 0 ? notable : all).slice(-8);
        return `${message}\nKernel output:\n  ${lines.join('\n  ')}`;
    }

    /**
     * Why a running kernel exited, with the error lines the kernels printed in
     * the seconds before (`[elara] FATAL: ...`): without them the reason is only
     * an exit code. Every R kernel's lines carry the same label, so only recent,
     * error-like lines are added, and the reason is returned unchanged when there
     * are none (or when the output was already printed or goes to a log file).
     */
    describeKernelExit(reason: string): string {
        if (this.forwardKernelOutput) return reason;
        const since = Date.now() - KERNEL_EXIT_OUTPUT_WINDOW_MS;
        const lines = this.recentOutput.filter((line) => line.at >= since && NOTABLE_OUTPUT.test(line.text)).map((line) => line.text);
        return lines.length === 0 ? reason : `${reason}\nKernel output:\n  ${lines.slice(-8).join('\n  ')}`;
    }

    /** The running supervisor (started, or in persistent mode found, on first use). */
    endpoint(): Promise<SupervisorEndpoint> {
        if (!this.readyPromise) {
            this.readyPromise = this.startOrFind();
            this.readyPromise.catch(() => { this.readyPromise = undefined; });
        }
        return this.readyPromise;
    }

    private async startOrFind(): Promise<SupervisorEndpoint> {
        if (this.persistent) {
            const known = readSupervisorState(this.persistent.stateFile);
            if (known && await this.answers(known)) {
                this.logger.info(`Reconnected to the supervisor (pid ${known.pid})`);
                return known;
            }
        }
        if (this.persistent) mkdirSync(dirname(this.persistent.stateFile), { recursive: true });
        const endpoint = await this.spawnSupervisor();
        if (this.persistent) writeSupervisorState(this.persistent.stateFile, endpoint);
        return endpoint;
    }

    // Whether a recorded supervisor is still there and takes its token.
    private async answers(endpoint: SupervisorEndpoint): Promise<boolean> {
        // A timer cleared as soon as it answers (not AbortSignal.timeout(),
        // whose timer outlives the request: a process exiting with it pending
        // trips an assertion in Node on Windows).
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 3000);
        try {
            const res = await fetch(`${endpoint.httpBase}/sessions`, {
                headers: supervisorHeaders(endpoint.token),
                signal: controller.signal
            });
            return res.ok;
        } catch {
            return false;
        } finally {
            clearTimeout(timer);
        }
    }

    private spawnSupervisor(): Promise<SupervisorEndpoint> {
        return new Promise((resolve, reject) => {
            const exePath = resolveSupervisorExecutable();
            this.logger.debug(`Spawning supervisor process from: ${exePath}`);

            const args = this.persistent ? ['--idle-shutdown-minutes', String(this.persistent.idleShutdownMinutes)] : [];
            // Persistent: its output (the kernels' start-up lines) goes to a
            // log file beside the state file instead of a pipe, which would
            // outlive this process (and, on Windows, trips libuv when a
            // process exits with such a pipe still open).
            const logFd = this.persistent ? openSync(supervisorLogFile(this.persistent.stateFile), 'a') : undefined;
            // The R kernel's log (ELARA_LOG_LEVEL) in full when its output is shown, unless set already.
            const env = this.forwardKernelOutput && !process.env.ELARA_LOG_LEVEL ? { ...process.env, ELARA_LOG_LEVEL: 'debug' } : process.env;
            const child = spawn(exePath, args, {
                env,
                stdio: ['ignore', 'pipe', logFd ?? 'pipe'],
                windowsHide: true,
                // Persistent: its own process group, so it is not taken down
                // with this process (Ctrl+C in a terminal, a window reload).
                detached: this.persistent !== undefined
            });
            this.child = child;

            if (logFd !== undefined) closeSync(logFd);
            if (child.stderr) {
                createInterface({ input: child.stderr }).on('line', (line) => {
                    this.rememberOutput(line);
                    if (this.forwardKernelOutput) process.stderr.write(`${line}\n`);
                });
            }

            const rl = createInterface({ input: child.stdout! });
            const onLine = (line: string) => {
                let message: { type?: string; httpPort?: number; wsPort?: number; token?: string; pid?: number };
                try {
                    message = JSON.parse(line);
                } catch {
                    return;
                }
                if (message.type === 'supervisorReady' && typeof message.httpPort === 'number' && typeof message.wsPort === 'number') {
                    cleanup();
                    this.logger.info(`Supervisor ready (pid ${child.pid})`, { httpPort: message.httpPort, wsPort: message.wsPort });
                    if (this.persistent) {
                        // Nothing more comes on stdout; let go of the pipe
                        // and the process, which must not keep this one alive.
                        rl.close();
                        child.stdout?.destroy();
                        child.unref();
                    }
                    resolve({
                        httpBase: `http://127.0.0.1:${message.httpPort}`,
                        wsBase: `ws://127.0.0.1:${message.wsPort}`,
                        token: message.token ?? '',
                        pid: message.pid ?? child.pid ?? 0
                    });
                }
            };
            const onExit = (code: number | null) => {
                cleanup();
                reject(new Error(`Supervisor process exited before it was ready (code ${code})`));
            };
            const cleanup = () => {
                rl.off('line', onLine);
                child.off('exit', onExit);
            };

            rl.on('line', onLine);
            child.once('error', (error) => { cleanup(); reject(error); });
            child.once('exit', onExit);
        });
    }

    async createSession(options: EngineOptions): Promise<SessionConnectionInfo> {
        const endpoint = await this.endpoint();

        const res = await fetch(`${endpoint.httpBase}/sessions`, {
            method: 'POST',
            headers: supervisorHeaders(endpoint.token, { 'content-type': 'application/json' }),
            body: JSON.stringify(buildSessionOptionsBody(options))
        });

        const body = await res.json() as CreateSessionResponse;
        if (!res.ok || !body.sessionId) {
            throw new Error(this.withKernelOutput(body.error ?? `Supervisor failed to create session (HTTP ${res.status})`));
        }

        return { sessionId: body.sessionId, httpBase: endpoint.httpBase, wsBase: endpoint.wsBase, token: endpoint.token };
    }

    /** Every session the supervisor has (all of them: it does not know which client created which). */
    async listSessions(): Promise<SupervisorSessionInfo[]> {
        const endpoint = await this.endpoint();
        const res = await fetch(`${endpoint.httpBase}/sessions`, { headers: supervisorHeaders(endpoint.token) });
        if (!res.ok) {
            throw new Error(`Could not list the supervisor's sessions (HTTP ${res.status})`);
        }
        return ((await res.json()) as { sessions: SupervisorSessionInfo[] }).sessions;
    }

    /** Connection details for a session that already exists in the supervisor. */
    async connectionFor(sessionId: string): Promise<SessionConnectionInfo> {
        const endpoint = await this.endpoint();
        return { sessionId, httpBase: endpoint.httpBase, wsBase: endpoint.wsBase, token: endpoint.token };
    }

    async stopSession(info: SessionConnectionInfo): Promise<void> {
        try {
            const res = await fetch(`${info.httpBase}/sessions/${info.sessionId}`, { method: 'DELETE', headers: supervisorHeaders(info.token) });
            await res.arrayBuffer(); // read to the end, so no request is left in flight
        } catch (error) {
            this.logger.warn(`Failed to gracefully stop session ${info.sessionId} via supervisor`, error);
        }
    }

    /**
     * Replaces a session's kernel process in place (SessionRegistry::
     * restartSession() on the native side stops the old kernel, spawns a
     * fresh one, and re-registers it under the *same* session id) -- so
     * unlike stopSession(), nothing about `info` changes here. Works both
     * to recover a crashed session and, same as Jupyter's "Restart Kernel",
     * to reset a healthy one.
     *
     * `options`, if given, replaces the R installation this session's next
     * kernel process launches with (rHome/rPath/etc) instead of reusing
     * whatever it was created with -- e.g. switching from R 4.4 to R 4.6
     * for an existing notebook connection on the fly, without needing to
     * close this session and create a new one just to pick a different R.
     */
    async restartSession(info: SessionConnectionInfo, options?: Partial<EngineOptions>): Promise<void> {
        const res = await fetch(`${info.httpBase}/sessions/${info.sessionId}/restart`, {
            method: 'POST',
            headers: supervisorHeaders(info.token, options ? { 'content-type': 'application/json' } : {}),
            ...(options ? { body: JSON.stringify(buildSessionOptionsBody(options)) } : {})
        });
        const body = await res.json() as CreateSessionResponse;
        if (!res.ok || !body.sessionId) {
            throw new Error(body.error ?? `Supervisor failed to restart session ${info.sessionId} (HTTP ${res.status})`);
        }
    }

    /**
     * Stops the supervisor for good: in persistent mode it is asked to shut
     * down (stopping every session it still has) and forgotten; otherwise
     * this is kill(true).
     */
    async shutdown(): Promise<void> {
        if (!this.persistent) {
            this.kill(true);
            return;
        }
        const endpoint = readSupervisorState(this.persistent.stateFile) ?? (this.readyPromise ? await this.readyPromise.catch(() => undefined) : undefined);
        if (endpoint) {
            try {
                const res = await fetch(`${endpoint.httpBase}/shutdown`, { method: 'POST', headers: supervisorHeaders(endpoint.token) });
                await res.arrayBuffer();
            } catch (error) {
                this.logger.warn('Could not ask the supervisor to shut down', error);
            }
        }
        try {
            rmSync(this.persistent.stateFile, { force: true });
        } catch {
            // Already gone.
        }
        this.readyPromise = undefined;
    }

    /**
     * Skips graceful per-session shutdown -- only for cleanup on the way out.
     * `expected` is the normal end of stopAll(), after every session was
     * stopped: not worth a warning. A persistent supervisor is left running
     * (that is its point); use shutdown() to stop it.
     */
    kill(expected = false): void {
        if (this.persistent) return;
        if (this.child && !this.child.killed) {
            const message = `Force-killing supervisor process (pid ${this.child.pid})`;
            if (expected) this.logger.debug(message);
            else this.logger.warn(message);
            this.child.kill();
        }
    }
}

// Finds the supervisor binary: JOVIAN_NATIVE_DIR, else the installed
// @scope/jovian-<os>-<cpu> package, else a source checkout's
// dist/native/Release (see native-paths.ts).
function resolveSupervisorExecutable(): string {
    const { dir } = locateNativeDirectory();
    // Electron keeps native files outside the asar archive.
    const nativeDir = dir.replace(/\bnode_modules\.asar\b/, 'node_modules.asar.unpacked');
    ensureExecutable(nativeDir);
    return join(nativeDir, process.platform === 'win32' ? 'themisto.exe' : 'themisto');
}
