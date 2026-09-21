import { spawn, type ChildProcess } from 'child_process';
import { createInterface } from 'readline';
import { join } from 'path';
import type { EngineOptions } from '../types/index.js';
import { Logger } from '../utils/logger.js';
import { bundledHeraSource, ensureExecutable, locateNativeDirectory } from './native-paths.js';

export interface SessionConnectionInfo {
    sessionId: string;
    httpBase: string;
    wsBase: string;
}

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
        heraSrcPath: options.heraSrcPath ?? bundledHeraSource(),
        pythonHome: options.pythonHome,
        pythonPath: options.pythonPath,
        venvPath: options.venvPath,
        workingDirectory: options.workingDirectory
    };
}

// themisto is the kernel supervisor: it's the only process in this system
// that ever links a native ZMQ
// binding. It spawns/owns `elara` kernel processes, speaks ZMQ to
// each of them, and re-exposes sessions over plain HTTP (lifecycle) +
// WebSocket (execute/interrupt/message streaming) -- so Electron/VS Code's
// process, where this class runs, never needs a native dependency at all.
export class SupervisorClient {
    private child: ChildProcess | undefined;
    private readyPromise: Promise<{ httpPort: number; wsPort: number }> | undefined;
    private readonly logger: Logger;
    private readonly forwardKernelOutput: boolean;
    // The supervisor's stderr is where every kernel's start-up output ends up
    // ([elara] ..., [carpo] ...). It is kept, not printed, unless asked for,
    // and attached to the error when a kernel fails to start.
    private readonly recentOutput: string[] = [];

    constructor(logger: Logger, options: { forwardKernelOutput?: boolean } = {}) {
        this.logger = logger;
        this.forwardKernelOutput = options.forwardKernelOutput ?? false;
    }

    private rememberOutput(line: string): void {
        if (!line.trim()) return;
        this.recentOutput.push(line);
        if (this.recentOutput.length > 200) this.recentOutput.shift();
    }

    /** The message plus what the kernels said just before, when that was not already printed. */
    private withKernelOutput(message: string): string {
        if (this.forwardKernelOutput || this.recentOutput.length === 0) return message;
        const notable = this.recentOutput.filter((line) => /error|fatal|warning|failed|cannot|not found|no such/i.test(line));
        const lines = (notable.length > 0 ? notable : this.recentOutput).slice(-8);
        return `${message}\nKernel output:\n  ${lines.join('\n  ')}`;
    }

    private ensureStarted(): Promise<{ httpPort: number; wsPort: number }> {
        if (!this.readyPromise) {
            this.readyPromise = this.spawnSupervisor();
        }
        return this.readyPromise;
    }

    private spawnSupervisor(): Promise<{ httpPort: number; wsPort: number }> {
        return new Promise((resolve, reject) => {
            const exePath = resolveSupervisorExecutable();
            this.logger.debug(`Spawning supervisor process from: ${exePath}`);

            const child = spawn(exePath, [], { stdio: ['ignore', 'pipe', 'pipe'] });
            this.child = child;

            createInterface({ input: child.stderr! }).on('line', (line) => {
                this.rememberOutput(line);
                if (this.forwardKernelOutput) process.stderr.write(`${line}\n`);
            });

            const rl = createInterface({ input: child.stdout! });
            const onLine = (line: string) => {
                let message: { type?: string; httpPort?: number; wsPort?: number };
                try {
                    message = JSON.parse(line);
                } catch {
                    return;
                }
                if (message.type === 'supervisorReady' && typeof message.httpPort === 'number' && typeof message.wsPort === 'number') {
                    cleanup();
                    this.logger.info(`Supervisor ready (pid ${child.pid})`, { httpPort: message.httpPort, wsPort: message.wsPort });
                    resolve({ httpPort: message.httpPort, wsPort: message.wsPort });
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
        const { httpPort, wsPort } = await this.ensureStarted();
        const httpBase = `http://127.0.0.1:${httpPort}`;

        const res = await fetch(`${httpBase}/sessions`, {
            method: 'POST',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify(buildSessionOptionsBody(options))
        });

        const body = await res.json() as CreateSessionResponse;
        if (!res.ok || !body.sessionId) {
            throw new Error(this.withKernelOutput(body.error ?? `Supervisor failed to create session (HTTP ${res.status})`));
        }

        return { sessionId: body.sessionId, httpBase, wsBase: `ws://127.0.0.1:${wsPort}` };
    }

    async stopSession(info: SessionConnectionInfo): Promise<void> {
        try {
            await fetch(`${info.httpBase}/sessions/${info.sessionId}`, { method: 'DELETE' });
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
            ...(options ? {
                headers: { 'content-type': 'application/json' },
                body: JSON.stringify(buildSessionOptionsBody(options))
            } : {})
        });
        const body = await res.json() as CreateSessionResponse;
        if (!res.ok || !body.sessionId) {
            throw new Error(body.error ?? `Supervisor failed to restart session ${info.sessionId} (HTTP ${res.status})`);
        }
    }

    /**
     * Skips graceful per-session shutdown -- only for cleanup on the way out.
     * `expected` is the normal end of stopAll(), after every session was
     * stopped: not worth a warning.
     */
    kill(expected = false): void {
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
