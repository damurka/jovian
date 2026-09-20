import { EventEmitter } from 'events';
import { randomUUID } from 'crypto';
import type {
    EngineOptions,
    ExecutionHistoryEntry,
    ExecutionOptions,
    ExecutionResult,
    KernelHistoryEntry,
    KernelHistoryOptions,
    LogLevel,
    ShinyAppHandle,
    ShinyAppOptions
} from '../types/index.js';
import type { JupyterMessage } from '../types/messages.js';
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
 * One R session running in its own OS process (elara, spawned and
 * supervised by themisto -- see lib/session/supervisor-client.ts),
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
// Local, in-memory only -- same reasoning as the playground's own
// MAX_HISTORY_CELLS (tools/playground/server.js): bounds a long-lived
// Session's memory use against a session that just keeps running forever,
// without needing every caller to remember to cap it themselves.
const MAX_EXECUTION_HISTORY_ENTRIES = 200;

export class Session extends EventEmitter {
    private ws: WebSocket | undefined;
    // Public (not just for this class's own use): callers that need to
    // talk to the supervisor's HTTP API directly for something this class
    // doesn't itself expose (e.g. the playground's PID/memory-usage
    // display, via GET {httpBase}/sessions/{sessionId}) can, instead of
    // needing a new method here for every such diagnostic.
    readonly info: SessionConnectionInfo;
    // The exact options this session was created with -- e.g. so a caller
    // that only has a `Session` handle (not the options it was originally
    // built from) can still answer "what R_HOME/PYTHONHOME is this",
    // without needing its own separate bookkeeping (a real gap: the
    // playground tool used to duplicate this into its own per-session
    // `entry.config` purely because nothing on Session itself exposed it).
    readonly options: EngineOptions;
    private readonly supervisor: SupervisorClient;
    private readonly logger: Logger;
    private readonly router: MessageRouter;
    private readonly middleware: MiddlewareChain;
    private readonly queue: ExecutionQueue;
    private readyPromise: Promise<void>;
    private stopped = false;

    // Every execute() call's code + the iopub messages it produced, bucketed
    // by the execute_request's own msg id (execute_input's parentMsgId) --
    // see getHistory()'s doc comment for what this is actually for.
    private readonly executionHistory: ExecutionHistoryEntry[] = [];
    private readonly executionHistoryByMsgId = new Map<string, ExecutionHistoryEntry>();

    constructor(info: SessionConnectionInfo, options: EngineOptions, supervisor: SupervisorClient) {
        super();
        this.info = info;
        this.options = options;
        this.supervisor = supervisor;
        this.logger = new Logger(options.logger);
        this.on('message', (message: JupyterMessage) => this.recordExecutionHistory(message));

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
            execute: (code: string, options: ExecutionOptions = {}): string => {
                const id = randomUUID();
                this.send({ type: 'execute', id, code, options });
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
                this.emit('exit', { reason: 'WebSocket connection to the supervisor closed unexpectedly' });
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
     *
     * `options`, if given, switches this session's R installation on the
     * restart (rHome/rPath/etc) instead of reusing whatever it was created
     * with -- e.g. flip from R 4.4 to R 4.6, the same way Positron's Ark
     * lets you switch R versions on the fly, without closing this session
     * and opening a new one (a different session id/WS URL) just to pick a
     * different R.
     */
    async restart(options?: Partial<EngineOptions>): Promise<void> {
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
            await this.supervisor.restartSession(this.info, options);
            await this.connect();
        })();

        await this.readyPromise;
        this.logger.info(`Session ${this.info.sessionId} restarted`);
        this.emit('restarted');
    }

    private send(frame: Record<string, unknown>): void {
        this.ws?.send(JSON.stringify(frame));
    }

    // Buckets every iopub message this session produces by which
    // execute_request it belongs to, purely from the messages themselves --
    // execute_input's own content.code/execution_count is enough to start a
    // new entry, so this needs no separate bookkeeping of the original
    // execute() call. Mirrors tools/playground/server.js's recordHistory(),
    // now available to every consumer of this library, not just that one
    // demo tool.
    private recordExecutionHistory(message: JupyterMessage): void {
        if (message.msgType === 'execute_input') {
            const entry: ExecutionHistoryEntry = {
                code: (message.content as { code?: string })?.code ?? '',
                executionCount: (message.content as { execution_count?: number })?.execution_count,
                time: Date.now(),
                messages: []
            };
            this.executionHistory.push(entry);
            this.executionHistoryByMsgId.set(message.parentMsgId, entry);
            if (this.executionHistory.length > MAX_EXECUTION_HISTORY_ENTRIES) {
                const removed = this.executionHistory.shift();
                if (removed) {
                    for (const [msgId, e] of this.executionHistoryByMsgId) {
                        if (e === removed) {
                            this.executionHistoryByMsgId.delete(msgId);
                            break;
                        }
                    }
                }
            }
            return;
        }
        // A stale input_request makes no sense to keep around -- by the
        // time anyone reads getHistory(), it's either long since been
        // answered (over the stdin channel, which never shows up as a
        // 'message' event -- see the class doc on sendInputReply()) or
        // whatever was blocked on it is long gone either way.
        if (message.msgType === 'input_request') {
            return;
        }
        const entry = this.executionHistoryByMsgId.get(message.parentMsgId);
        if (entry) {
            entry.messages.push(message);
        }
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
                    const reason = typeof frame.reason === 'string' ? frame.reason : 'unknown reason';
                    this.logger.error(`R session process for ${this.info.sessionId} exited unexpectedly: ${reason}`);
                    this.emit('exit', { reason });
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
     * Answers a pending input_request -- this session emits one (see the
     * 'input_request' event, content: {prompt, password}) whenever the
     * kernel calls input()/readline()/scan() during an execute() that was
     * given { allowStdin: true }, and genuinely blocks its single execution
     * thread until this arrives (ServerZmqImpl::sendStdin() in
     * native/src/adrastea/transport/server/server_zmq_impl.cpp does a real,
     * untimed ZMQ recv underneath). Fire-and-forget like interrupt(): the
     * reply that eventually unblocks the kernel surfaces through the
     * *execute_request's own* execute_reply/stream messages, not through a
     * reply to this call.
     */
    sendInputReply(value: string): void {
        this.send({ type: 'inputReply', value });
    }

    /**
     * This session's own local record of every execute() call it has made
     * and what each one produced (code + every iopub message), for as long
     * as this Session object has been alive. Purely in-memory and
     * process-local -- gone if the process holding this Session restarts,
     * same as the Session object itself. Useful for e.g. rebuilding a UI's
     * transcript after some *other* thing (not this process) reconnects to
     * it, or for inspecting what actually ran without threading your own
     * bookkeeping through every execute() call site.
     *
     * Not the same thing as queryKernelHistory(): this is this session's
     * own bookkeeping (full fidelity -- includes actual output, which the
     * kernel's own history manager doesn't track), while that one asks the
     * *kernel itself* what it remembers running (input code only,
     * authoritative even if some other client executed it, but capped by
     * this process's own historical view of it, and lost across a kernel
     * restart the same as the kernel's own memory of it is).
     */
    getHistory(): ExecutionHistoryEntry[] {
        return this.executionHistory;
    }

    /**
     * Sends a real Jupyter history_request and resolves with the kernel's
     * own history_reply (KernelCore::historyRequest() ->
     * HistoryManager::processRequest(), native/src/adrastea/core/history/)
     * -- the kernel's own authoritative record of what it has executed,
     * independent of which client (or how many, over how many reconnects)
     * actually ran it. Defaults to the 100 most recent executions ('tail').
     * See KernelHistoryOptions' own doc comment for the other access modes,
     * and getHistory()'s doc comment for how this differs from that.
     */
    async queryKernelHistory(options: KernelHistoryOptions = {}): Promise<KernelHistoryEntry[]> {
        await this.readyPromise;
        const id = randomUUID();
        const timeoutMs = 10000;

        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.off('message', onMessage);
                reject(new Error(`Timed out waiting for a history_reply after ${timeoutMs}ms`));
            }, timeoutMs);

            const onMessage = (message: JupyterMessage) => {
                if (message.msgType !== 'history_reply' || message.parentMsgId !== id) {
                    return;
                }
                clearTimeout(timer);
                this.off('message', onMessage);
                const content = message.content as { status?: string; ename?: string; evalue?: string; history?: KernelHistoryEntry[] };
                if (content.status === 'error') {
                    reject(new Error(content.evalue ?? content.ename ?? 'history_request failed'));
                    return;
                }
                resolve(content.history ?? []);
            };
            this.on('message', onMessage);

            this.send({ type: 'history', id, options });
        });
    }

    /**
     * Sends interrupt_request over the control channel (ws_relay.cpp's
     * `type === 'interrupt'` branch -> SessionRegistry::sendInterrupt()) --
     * fire-and-forget, matching this control-channel path's current shape:
     * nothing pumps interrupt_reply back over the WebSocket yet (pollLoop()
     * in session_registry.cpp only reads iopub/shell, not control), so
     * there's no reply to await here. Interrupting a kernel that isn't
     * currently blocked in a long-running call is a harmless no-op from the
     * caller's perspective either way.
     */
    interrupt(): void {
        this.send({ type: 'interrupt', id: randomUUID() });
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
