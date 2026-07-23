import { spawn, type ChildProcess } from 'child_process';
import { existsSync } from 'fs';
import { EventEmitter } from 'events';
import { randomUUID } from 'crypto';
import { fileURLToPath } from 'url';
import { delimiter, dirname, join } from 'path';
import type { EngineOptions, ExecutionOptions, ExecutionResult, ShinyAppOptions } from '../types/index.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const WORKER_PATH = join(__dirname, 'session-worker.js');

/**
 * Finds a real Node.js executable to run the R session with. Under plain
 * Node, process.execPath already is one. Under Electron (e.g. VS Code's
 * Shared Process), child_process.fork() always targets process.execPath --
 * Electron's own binary -- and even with ELECTRON_RUN_AS_NODE=1, loading
 * this addon there reliably crashes (0xC0000005) on Windows: reproduced
 * with fork() and with direct in-process creation, and with both a
 * plain-Node-built and an Electron-ABI-built addon, so it isn't a fork
 * mechanism or ABI issue -- something about the addon's native startup
 * (AllocConsole()/freopen_s() in RInterpreter's constructor) doesn't
 * tolerate Electron's process/sandbox model. A genuinely separate,
 * real node.exe process is the one thing that worked in every test.
 */
function resolveNodeExecutable(): string {
    if (!process.versions.electron) {
        return process.execPath;
    }

    if (process.env.DATASUITE_NODE_PATH && existsSync(process.env.DATASUITE_NODE_PATH)) {
        return process.env.DATASUITE_NODE_PATH;
    }

    const exeName = process.platform === 'win32' ? 'node.exe' : 'node';
    for (const dir of (process.env.PATH ?? '').split(delimiter)) {
        const candidate = join(dir, exeName);
        if (existsSync(candidate)) {
            return candidate;
        }
    }

    throw new Error(
        'datasuite-r: running inside Electron and could not find a standalone Node.js executable on PATH ' +
        '(required to run R sessions outside Electron\'s own runtime). Set DATASUITE_NODE_PATH to a node ' +
        'executable, or ensure Node.js is installed and on PATH.'
    );
}

export interface ShinyAppHandle {
    host: string;
    port: number;
    url: string;
    done: Promise<ExecutionResult>;
}

interface Pending {
    resolve: (value: any) => void;
    reject: (error: Error) => void;
}

function reviveResult(raw: any): ExecutionResult {
    return {
        ...raw,
        error: raw?.error ? Object.assign(new Error(raw.error.message), { name: raw.error.name }) : undefined
    };
}

/**
 * One R session running in its own OS process (via child_process.fork()),
 * proxying execute()/createShiny()/stop() over IPC. This is what makes
 * concurrent sessions safe: each process embeds exactly one R interpreter,
 * matching R's own one-per-process limit, instead of sharing one across
 * multiple DatasuiteEngine objects in the same process (which starves
 * whichever session isn't currently holding it -- see the crash this
 * replaces: a Shiny app blocking the interpreter while a second session's
 * execute() call timed out waiting for a turn that never came).
 */
export class Session extends EventEmitter {
    private readonly child: ChildProcess;
    private readonly pending = new Map<string, Pending>();
    private readonly shinyDoneWaiters = new Map<string, (result: ExecutionResult) => void>();
    private readonly readyPromise: Promise<void>;
    private stopped = false;

    constructor(options: EngineOptions) {
        super();

        // spawn(), not fork(): fork() always targets process.execPath, which
        // under Electron is Electron's own binary -- see
        // resolveNodeExecutable()'s comment for why that reliably crashes
        // this addon regardless of ELECTRON_RUN_AS_NODE. spawn() with an
        // explicit, genuinely separate node executable plus stdio: [...,
        // 'ipc'] gives the exact same fork()-style IPC (child.send() /
        // 'message' events) without ever touching Electron's binary.
        this.child = spawn(resolveNodeExecutable(), [WORKER_PATH], {
            // 'pipe', not 'inherit': the parent may not have real console
            // stdio at all (e.g. VS Code's Shared Process is a headless
            // background process) -- inheriting whatever it has is risky
            // given the native addon's RInterpreter constructor calls
            // AllocConsole()+freopen_s() on Windows right at startup, before
            // any R code runs. 'pipe' gives the child well-defined,
            // Node-managed stdio regardless of the parent's own state.
            stdio: ['ignore', 'pipe', 'pipe', 'ipc'],
            env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' }
        });

        // Forward the piped output for visibility (e.g. during interactive
        // debugging) now that it's no longer inherited automatically.
        this.child.stdout?.on('data', (chunk: Buffer) => process.stdout.write(chunk));
        this.child.stderr?.on('data', (chunk: Buffer) => process.stderr.write(chunk));

        this.readyPromise = new Promise((resolve, reject) => {
            const onMessage = (message: any) => {
                if (message.type === 'workerReady') {
                    cleanup();
                    resolve();
                } else if (message.type === 'startError') {
                    cleanup();
                    reject(new Error(message.error));
                }
            };
            const onExit = (code: number | null) => {
                if (!this.stopped) {
                    cleanup();
                    reject(new Error(`Session process exited before it was ready (code ${code})`));
                }
            };
            const cleanup = () => {
                this.child.off('message', onMessage);
                this.child.off('exit', onExit);
            };

            this.child.on('message', onMessage);
            this.child.on('error', reject);
            this.child.on('exit', onExit);
        });

        this.child.on('message', (message: any) => this.handleMessage(message));
        this.child.send({ type: 'init', options });
    }

    private handleMessage(message: any): void {
        switch (message.type) {
            case 'event':
                this.emit(message.event, ...message.args);
                break;
            case 'executeResult':
                this.pending.get(message.id)?.resolve(reviveResult(message.result));
                this.pending.delete(message.id);
                break;
            case 'executeError':
                this.pending.get(message.id)?.reject(new Error(message.error));
                this.pending.delete(message.id);
                break;
            case 'shinyReady':
                this.pending.get(message.id)?.resolve({ host: message.host, port: message.port, url: message.url });
                this.pending.delete(message.id);
                break;
            case 'shinyStartError':
                this.pending.get(message.id)?.reject(new Error(message.error));
                this.pending.delete(message.id);
                break;
            case 'shinyDone':
                this.shinyDoneWaiters.get(message.id)?.(reviveResult(message.result));
                this.shinyDoneWaiters.delete(message.id);
                break;
            case 'stopped':
                this.stopped = true;
                break;
        }
    }

    /** Resolves once this session's R interpreter has started. */
    ready(): Promise<void> {
        return this.readyPromise;
    }

    async execute(code: string, options: ExecutionOptions = {}): Promise<ExecutionResult> {
        await this.readyPromise;
        const id = randomUUID();
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.child.send({ id, type: 'execute', code, options });
        });
    }

    async createShiny(options: ShinyAppOptions): Promise<ShinyAppHandle> {
        await this.readyPromise;
        const id = randomUUID();

        const done = new Promise<ExecutionResult>((resolve) => {
            this.shinyDoneWaiters.set(id, resolve);
        });

        const { host, port, url } = await new Promise<{ host: string; port: number; url: string }>((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.child.send({ id, type: 'createShiny', options });
        });

        return { host, port, url, done };
    }

    /** Stops the R session and waits for its process to exit. */
    async stop(): Promise<void> {
        if (this.stopped) return;
        await new Promise<void>((resolve) => {
            this.child.once('exit', () => resolve());
            this.child.send({ type: 'stop' });
        });
    }

    /** Skips the graceful shutdown protocol -- only for cleanup on the way out. */
    kill(): void {
        if (!this.stopped) this.child.kill();
    }
}

export class SessionManager {
    private readonly sessions = new Set<Session>();
    private exitHandlerRegistered = false;

    /** Creates a new R session in its own OS process and waits for it to be ready. */
    async createSession(options: EngineOptions = {}): Promise<Session> {
        const session = new Session(options);
        this.sessions.add(session);
        this.registerExitHandler();

        try {
            await session.ready();
        } catch (error) {
            this.sessions.delete(session);
            throw error;
        }

        return session;
    }

    /** Gracefully stops every session managed by this instance. */
    async stopAll(): Promise<void> {
        await Promise.all([...this.sessions].map((session) => session.stop()));
        this.sessions.clear();
    }

    /**
     * Forcibly terminates every session's process. Prefer stopAll(), but a
     * session whose R interpreter is blocked in a long-running call (e.g.
     * shiny::runApp()) can't process a graceful shutdown_request until that
     * call returns -- callers wanting a bounded-time exit (e.g. a Ctrl+C
     * handler) should race stopAll() against a timeout and fall back to this.
     */
    killAll(): void {
        for (const session of this.sessions) session.kill();
        this.sessions.clear();
    }

    // Safety net: if the parent process exits (including via Ctrl+C) without
    // an explicit stopAll(), don't leave child processes (and whatever R
    // session/Shiny app they're running) orphaned in the background.
    private registerExitHandler(): void {
        if (this.exitHandlerRegistered) return;
        this.exitHandlerRegistered = true;
        process.once('exit', () => {
            for (const session of this.sessions) session.kill();
        });
    }
}
