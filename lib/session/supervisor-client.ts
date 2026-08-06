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

// datasuite-supervisor plays the role Positron's Kallichore plays for Ark:
// it's the only process in this system that ever links a native ZMQ
// binding. It spawns/owns `datasuite-r` kernel processes, speaks ZMQ to
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
            body: JSON.stringify({
                rHome: options.rHome,
                rPath: options.rPath,
                rLibs: options.rLibs,
                pandocPath: options.pandocPath,
                heraSrcPath: options.heraSrcPath
            })
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

    /** Skips graceful per-session shutdown -- only for cleanup on the way out. */
    kill(): void {
        if (this.child && !this.child.killed) {
            this.logger.warn(`Force-killing supervisor process (pid ${this.child.pid})`);
            this.child.kill();
        }
    }
}

// Finds the supervisor binary built alongside the addon (see
// native/CMakeLists.txt's DATASUITE_BUILD_SUPERVISOR target and
// CMAKE_RUNTIME_OUTPUT_DIRECTORY = dist/native/$<CONFIG>, which
// scripts/build.js copies/references the same way addon-loader.ts does for
// datasuite_addon.node).
function resolveSupervisorExecutable(): string {
    const exeName = process.platform === 'win32' ? 'datasuite-supervisor.exe' : 'datasuite-supervisor';
    const candidate = join(__dirname, '../../native/Release', exeName);
    if (!existsSync(candidate)) {
        throw new Error(
            `datasuite-r: supervisor executable not found at ${candidate}. ` +
            `Run the native build (npm run build:native) before creating a session.`
        );
    }
    return candidate;
}
