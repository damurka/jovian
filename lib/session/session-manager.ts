import { EventEmitter } from 'events';
import { randomUUID } from 'crypto';
import type { EngineOptions, ExecutionOptions, ExecutionResult, LogLevel, ShinyAppHandle, ShinyAppOptions } from '../types/index.js';
import { Logger } from '../utils/logger.js';
import { MessageRouter } from '../messaging/message-router.js';
import { ExecutionQueue } from '../execution/execution-queue.js';
import { MiddlewareChain } from '../middleware/middleware-chain.js';
import { LoggingMiddleware } from '../middleware/plugins/logging-plugin.js';
import { MetricsMiddleware } from '../middleware/plugins/metrics-plugin.js';
import { StreamHandler } from '../handlers/stream-handler.js';
import { ResultHandler } from '../handlers/result-handler.js';
import { ErrorHandler } from '../handlers/error-handler.js';
import { DisplayHandler } from '../handlers/display-handler.js';
import { findFreePort, waitForPort } from '../utils/network.js';
import { SupervisorClient, type SessionConnectionInfo } from './supervisor-client.js';

// Reuses lib/types/engine.ts's ShinyAppHandle instead of declaring a
// second, structurally-identical interface here -- lib/index.ts used to
// re-export this one under a SessionShinyAppHandle alias purely to dodge a
// name collision with `export * from './types/index.js'`; now that this is
// the same binding, the alias is just a second name for the same type.
export type { ShinyAppHandle };

interface WsFrame {
    type: string;
    [key: string]: unknown;
}

/**
 * One R session running in its own OS process (datasuite-r, spawned and
 * supervised by datasuite-supervisor -- see lib/session/supervisor-client.ts),
 * proxying execute()/createShiny()/stop() over a per-session WebSocket. The
 * supervisor is the only process in this tree that ever links a native ZMQ
 * binding; this class only ever does plain HTTP/WS, so it's safe to run
 * inside Electron/VS Code's Shared Process without any native-addon-loading
 * concerns.
 *
 * The MessageRouter/handlers/ExecutionQueue/MiddlewareChain pipeline below
 * doesn't care where its raw JSON envelope strings come from -- here, that's
 * the WebSocket's 'message' frames.
 */
export class Session extends EventEmitter {
    private ws: WebSocket | undefined;
    private readonly info: SessionConnectionInfo;
    private readonly supervisor: SupervisorClient;
    private readonly logger: Logger;
    private readonly router: MessageRouter;
    private readonly middleware: MiddlewareChain;
    private readonly queue: ExecutionQueue;
    private readyPromise: Promise<void>;
    private stopped = false;

    constructor(info: SessionConnectionInfo, options: EngineOptions, supervisor: SupervisorClient) {
        super();
        this.info = info;
        this.supervisor = supervisor;
        this.logger = new Logger(options.logger);

        this.router = new MessageRouter(this);
        this.router.registerHandler('stream', new StreamHandler());
        this.router.registerHandler('execute_result', new ResultHandler());
        this.router.registerHandler('display_data', new DisplayHandler());
        this.router.registerHandler('error', new ErrorHandler());

        this.middleware = new MiddlewareChain();
        if (options.enableLogging) {
            this.middleware.use(new LoggingMiddleware());
        }
        if (options.enableMetrics) {
            this.middleware.use(new MetricsMiddleware());
        }

        // Adapter exposing the same `{ execute(code): msgId }` shape the
        // native addon used to provide directly -- ExecutionQueue's
        // single-flight/timeout/msgId-correlation logic
        // (lib/execution/execution-queue.ts) is reused completely
        // unmodified, it just sends over the WebSocket now instead of
        // calling into an in-process addon. The id is generated here
        // (client-side) rather than returned from the "addon", since the
        // supervisor has no synchronous return path over a WS send.
        const wsAddon = {
            execute: (code: string): string => {
                const id = randomUUID();
                this.send({ type: 'execute', id, code });
                return id;
            }
        };
        this.queue = new ExecutionQueue(wsAddon, this, options.queueSize, this.logger);

        this.readyPromise = this.connect();
    }

    /**
     * (Re)establishes the WebSocket to this.info's session and resolves
     * once it's ready. Used both by the constructor and by restart() --
     * info.sessionId/httpBase/wsBase don't change across a restart
     * (SessionRegistry::restartSession() replaces the kernel in place under
     * the same id), so reconnecting to the exact same URL is enough to pick
     * back up a session the supervisor just gave a fresh kernel.
     */
    private connect(): Promise<void> {
        // Defensive, not just for restart()'s benefit: closing an
        // already-closed/undefined socket is a no-op, so this is safe to
        // call unconditionally even from the constructor where this.ws is
        // still undefined.
        this.ws?.close();

        return new Promise((resolve, reject) => {
            const url = `${this.info.wsBase}/sessions/${this.info.sessionId}/messages`;
            this.logger.debug(`Connecting to session ${this.info.sessionId} at ${url}`);
            const ws = new WebSocket(url);
            this.ws = ws;

            const onOpenError = () => reject(new Error(`WebSocket connection to session ${this.info.sessionId} failed`));
            const onCloseBeforeReady = () => reject(new Error(`Session ${this.info.sessionId} closed before it was ready`));
            const onReady = () => {
                cleanup();
                resolve();
            };
            const cleanup = () => {
                ws.removeEventListener('error', onOpenError);
                ws.removeEventListener('close', onCloseBeforeReady);
            };

            ws.addEventListener('error', onOpenError);
            ws.addEventListener('close', onCloseBeforeReady);
            ws.addEventListener('message', (event: MessageEvent) => {
                void this.handleFrame(String(event.data), onReady);
            });

            // Unlike the ready-phase handlers above (removed once ready
            // resolves), this listener stays for this socket's whole
            // lifetime. Without it, a kernel crash mid-execution left every
            // pending execute()/createShiny() call hanging forever --
            // nothing else ever settles those promises. Mirrors the old
            // child.on('exit') handler this replaces.
            //
            // `this.ws !== ws` guards against a stale event from a socket
            // restart() already superseded: closing the old one above is
            // async from the browser/runtime WebSocket's perspective, so
            // its 'close' can still fire after this.ws has moved on to a
            // newer connection.
            ws.addEventListener('close', () => {
                if (this.stopped || this.ws !== ws) {
                    return;
                }
                this.logger.error(`Session ${this.info.sessionId} connection closed unexpectedly`);
                this.emit('exit', {});
                this.queue.clear();
            });
        });
    }

    /**
     * Replaces this session's kernel process in place, keeping the same
     * session id -- recovers a crashed session (kernelExit/unexpected close
     * leaves the Session object itself alive but every execute() rejecting
     * forever otherwise), and doubles as Jupyter's "Restart Kernel" for a
     * still-healthy one. Not available after an explicit stop()/kill(): at
     * that point the caller's intent was to end the session, not reset it
     * -- create a new one instead via SessionManager.createSession().
     */
    async restart(): Promise<void> {
        if (this.stopped) {
            throw new Error(`Cannot restart session ${this.info.sessionId}: it was already stopped`);
        }

        this.logger.info(`Restarting session ${this.info.sessionId}`);
        this.queue.clear();

        // Reassigned synchronously, before awaiting anything below, so a
        // concurrent execute()/createShiny() call that reads this.readyPromise
        // while the restart is still in flight waits for the new connection
        // instead of racing the old (already-dead-or-dying) one.
        this.readyPromise = (async () => {
            await this.supervisor.restartSession(this.info);
            await this.connect();
        })();

        await this.readyPromise;
        this.logger.info(`Session ${this.info.sessionId} restarted`);
        this.emit('restarted');
    }

    private send(frame: Record<string, unknown>): void {
        this.ws?.send(JSON.stringify(frame));
    }

    private async handleFrame(text: string, onReady: () => void): Promise<void> {
        let frame: WsFrame;
        try {
            frame = JSON.parse(text);
        } catch {
            return;
        }

        switch (frame.type) {
            case 'ready':
                onReady();
                break;

            case 'message':
                try {
                    const processed = await this.middleware.process(text);
                    await this.router.route(processed);
                } catch (error) {
                    this.logger.error('Error handling message', error);
                    this.emit('error', error);
                }
                break;

            case 'log': {
                // A logger callback can't cross the process boundary to the
                // supervisor/kernel, so log output arrives as
                // {type:'log', level, message, data} frames instead and gets
                // replayed through this session's own Logger (built from the
                // original caller-supplied callback) here.
                const level: LogLevel = (frame.level as LogLevel) ?? 'info';
                this.logger[level](String(frame.message ?? ''), frame.data);
                break;
            }

            case 'kernelExit':
                if (!this.stopped) {
                    this.logger.error(`R session process for ${this.info.sessionId} exited unexpectedly`);
                    this.emit('exit', {});
                    this.queue.clear();
                }
                break;

            default:
                break;
        }
    }

    /** Resolves once this session's R interpreter has started. */
    ready(): Promise<void> {
        return this.readyPromise;
    }

    async execute(code: string, options: ExecutionOptions = {}): Promise<ExecutionResult> {
        await this.readyPromise;
        return this.queue.execute(code, options);
    }

    /**
     * Launches a Shiny app in this session's R process and resolves once
     * it's actually accepting connections. shiny::runApp() blocks the R
     * session for as long as the app runs, so -- unlike execute() --
     * resolving here does not mean the app is done; that's what the
     * returned `done` promise is for.
     */
    async createShiny(options: ShinyAppOptions): Promise<ShinyAppHandle> {
        await this.readyPromise;

        const host = options.host ?? '127.0.0.1';
        const port = options.port ?? await findFreePort(host);
        const launchBrowser = options.launchBrowser ?? false;
        const readyTimeout = options.readyTimeout ?? 10000;

        const appDir = rStringLiteral(options.appDir.replace(/\\/g, '/'));
        const setEnvPrefix = buildSetEnvCode(options.env);
        const code = `${setEnvPrefix}shiny::runApp(${appDir}, port = ${port}, host = '${host}', launch.browser = ${launchBrowser ? 'TRUE' : 'FALSE'})`;

        this.logger.info('Starting Shiny app', { appDir: options.appDir, host, port, readyTimeout });

        // timeout: 0 -- this call is expected to block indefinitely.
        const done = this.execute(code, { timeout: 0 });
        done.then(
            (result) => this.logger.info(`Shiny app at ${host}:${port} exited`, { success: result.success }),
            (error) => this.logger.error(`Shiny app at ${host}:${port} execution failed`, error)
        );

        const earlyExit = done.then((result) => {
            throw new Error(
                `Shiny app exited before it started listening (status: ${result.success ? 'ok' : 'error'})`
            );
        });
        earlyExit.catch(() => {});

        try {
            await Promise.race([waitForPort(host, port, readyTimeout), earlyExit]);
        } catch (error) {
            this.logger.error(`Shiny app at ${host}:${port} failed to start`, error);
            throw error;
        }

        this.logger.info(`Shiny app listening at http://${host}:${port}`);
        return { host, port, url: `http://${host}:${port}`, done };
    }

    /** Stops the R session and waits for its process to exit. */
    async stop(): Promise<void> {
        if (this.stopped) {
            return;
        }
        this.stopped = true;
        this.logger.info(`Stopping session ${this.info.sessionId}`);
        this.queue.clear();
        await this.supervisor.stopSession(this.info);
        this.ws?.close();
        this.emit('stopped');
    }

    /** Skips the graceful shutdown protocol -- only for cleanup on the way out. */
    kill(): void {
        if (!this.stopped) {
            this.stopped = true;
            this.logger.warn(`Force-closing session ${this.info.sessionId}`);
            // Without this, an execute() call still in flight when kill()
            // runs (e.g. one blocked waiting on a crashed kernel) never
            // settles -- closing the socket alone doesn't reject it.
            this.queue.clear();
            this.ws?.close();
        }
    }
}

export class SessionManager {
    private readonly supervisor = new SupervisorClient(new Logger());
    private readonly sessions = new Set<Session>();
    private exitHandlerRegistered = false;

    /** Creates a new R session in its own OS process and waits for it to be ready. */
    async createSession(options: EngineOptions = {}): Promise<Session> {
        const info = await this.supervisor.createSession(options);
        const session = new Session(info, options, this.supervisor);
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
        this.supervisor.kill();
    }

    /**
     * Forcibly terminates every session. Prefer stopAll(), but a session
     * whose R interpreter is blocked in a long-running call (e.g.
     * shiny::runApp()) can't process a graceful shutdown until that call
     * returns -- callers wanting a bounded-time exit (e.g. a Ctrl+C
     * handler) should race stopAll() against a timeout and fall back to this.
     */
    killAll(): void {
        for (const session of this.sessions) session.kill();
        this.sessions.clear();
        this.supervisor.kill();
    }

    // Safety net: if the parent process exits (including via Ctrl+C) without
    // an explicit stopAll(), don't leave the supervisor and its spawned
    // kernel processes orphaned in the background.
    private registerExitHandler(): void {
        if (this.exitHandlerRegistered) return;
        this.exitHandlerRegistered = true;
        process.once('exit', () => {
            for (const session of this.sessions) session.kill();
            this.supervisor.kill();
        });
    }
}

function rStringLiteral(value: string): string {
    return `'${value.replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;
}

function buildSetEnvCode(env: Record<string, string> | undefined): string {
    if (!env || Object.keys(env).length === 0) {
        return '';
    }

    const args = Object.entries(env)
        .map(([key, value]) => `${rStringLiteral(key)} = ${rStringLiteral(value)}`)
        .join(', ');

    return `Sys.setenv(${args}); `;
}
