import { spawn, type ChildProcess } from 'child_process';
import { createInterface } from 'readline';
import { existsSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import type { EngineOptions } from '../types/index.js';
import { Logger } from '../utils/logger.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

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
        heraSrcPath: options.heraSrcPath,
        pythonHome: options.pythonHome,
        pythonPath: options.pythonPath,
        venvPath: options.venvPath
    };
}

// themisto plays the role Positron's Kallichore plays for Ark:
// it's the only process in this system that ever links a native ZMQ
// binding. It spawns/owns `elara` kernel processes, speaks ZMQ to
// each of them, and re-exposes sessions over plain HTTP (lifecycle) +
// WebSocket (execute/interrupt/message streaming) -- so Electron/VS Code's
// process, where this class runs, never needs a native dependency at all.
export class SupervisorClient {
    private child: ChildProcess | undefined;
    private readyPromise: Promise<{ httpPort: number; wsPort: number }> | undefined;
    private readonly logger: Logger;

    constructor(logger: Logger) {
        this.logger = logger;
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

            child.stderr?.on('data', (chunk: Buffer) => process.stderr.write(chunk));

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
            throw new Error(body.error ?? `Supervisor failed to create session (HTTP ${res.status})`);
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
     * for an existing notebook connection, the same way Positron's Ark lets
     * you switch R versions on the fly, without needing to close this
     * session and create a new one just to pick a different R.
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

    /** Skips graceful per-session shutdown -- only for cleanup on the way out. */
    kill(): void {
        if (this.child && !this.child.killed) {
            this.logger.warn(`Force-killing supervisor process (pid ${this.child.pid})`);
            this.child.kill();
        }
    }
}

// Finds the supervisor binary (see native/CMakeLists.txt's
// JOVIAN_BUILD_THEMISTO target and CMAKE_RUNTIME_OUTPUT_DIRECTORY =
// dist/native/$<CONFIG>).
function resolveSupervisorExecutable(): string {
    // const exeName = process.platform === 'win32' ? 'themisto.exe' : 'themisto';
    // const candidate = join(__dirname, '../../native/Release', exeName);
    // if (!existsSync(candidate)) {
    //     throw new Error(
    //         `jovian: supervisor executable not found at ${candidate}. ` +
    //         `Run the native build (npm run build:native) before creating a session.`
    //     );
    // }
    const exeName = process.platform === 'win32' ? 'themisto.exe' : 'themisto';
    const candidate = join(__dirname, '../../native/Release', exeName)
        .replace(/\bnode_modules\.asar\b/, 'node_modules.asar.unpacked');
    if (!existsSync(candidate)) {
        throw new Error(`jovian: supervisor executable not found at ${candidate}. ` +
            `Run the native build (npm run build:native) before creating a session.`);
    }
    return candidate;
}
