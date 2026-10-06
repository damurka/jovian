import { EventEmitter } from 'events';
import { randomUUID } from 'crypto';
import type {
    BusyRequestOptions,
    CommInfoReplyContent,
    CompleteReplyContent,
    EngineOptions,
    ExecutionHistoryEntry,
    ExecutionOptions,
    ExecutionResult,
    InspectReplyContent,
    InterruptReplyContent,
    IsCompleteReplyContent,
    KernelHistoryEntry,
    KernelHistoryOptions,
    KernelInfoReplyContent,
    LogLevel,
    LoggerFunction,
    LogThreshold,
    DapResponse,
    SessionManagerOptions,
    UiRequest,
    SessionStatusInfo,
    ShinyAppHandle,
    SessionVariable,
    TablePage
} from '../types/index.js';
import type { ExecutionState, JupyterMessage } from '../types/messages.js';
import { Logger, defaultLogLevel } from '../utils/logger.js';
import { MessageRouter } from '../messaging/message-router.js';
import { ExecutionQueue } from '../execution/execution-queue.js';
import { MiddlewareChain } from '../middleware/middleware-chain.js';
import { LoggingMiddleware } from '../middleware/plugins/logging-plugin.js';
import { MetricsMiddleware } from '../middleware/plugins/metrics-plugin.js';
import { StreamHandler } from '../handlers/stream-handler.js';
import { ResultHandler } from '../handlers/result-handler.js';
import { ErrorHandler } from '../handlers/error-handler.js';
import { DisplayHandler } from '../handlers/display-handler.js';
import { SupervisorClient, sessionSocketUrl, supervisorHeaders, type SessionConnectionInfo, type SupervisorSessionInfo } from './supervisor-client.js';
import { homedir, tmpdir } from 'os';
import { delimiter, join as joinPath, resolve as resolvePath } from 'path';
import { withAbsolutePaths, withDiscoveredRuntime } from './runtimes.js';
import { analyzeR, type RCodeFacts } from './r-static.js';
import { RHelper, R_STATE_EXPRESSION, R_STATE_KEY, mergeCompletions, parseRState, type RSessionState } from './r-helper.js';
import { ensureRPackageIn, type EnsureRPackageOptions, type EnsureRPackageRequest, type RPackageResult, type RPackagesHost } from './r-packages.js';
import { ensurePythonEnvironmentIn, ensurePythonPackagesIn, type PythonPackageOptions, type PythonPackageRequest, type PythonPackageResult, type PythonPackagesHost } from './python-packages.js';
import { Comm } from './comm.js';
import { RSession } from './r-session.js';
import { StataSession } from './stata-session.js';

/** How long ensureRPackage()'s packages session waits for the next install before it stops. */
const PACKAGES_SESSION_IDLE_MS = 5 * 60 * 1000;

/** A library path in one form, to compare (Windows paths are case-insensitive, with either slash). */
function libraryKey(library: string): string {
    const resolved = resolvePath(library).replace(/[\\/]+$/, '');
    return process.platform === 'win32' ? resolved.replace(/\//g, '\\').toLowerCase() : resolved;
}

/** The library an R session installs into and loads from first (its rLibs' first entry), in libraryKey() form. */
function firstLibrary(options: EngineOptions): string | undefined {
    if (options.kernelType && options.kernelType !== 'r') return undefined;
    const first = options.rLibs?.split(delimiter).find((library) => library.length > 0);
    return first ? libraryKey(first) : undefined;
}

/** The virtual environment a Python session loads packages from (its venvPath), in libraryKey() form. */
function sessionVenv(options: EngineOptions): string | undefined {
    return options.kernelType === 'python' && options.venvPath ? libraryKey(options.venvPath) : undefined;
}

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

// A request() waiting for its reply: settled by the shell/control message
// whose parent_msg_id is the request's id (see Session.settleRequest()).
interface PendingRequest {
    replyType: string;
    resolve: (content: unknown) => void;
    reject: (error: Error) => void;
    timer: ReturnType<typeof setTimeout>;
}

const DEFAULT_REQUEST_TIMEOUT_MS = 10000;

// An R cell still running after this long gets its helper R process (see
// r-helper.ts) started and its packages attached, so the helper is ready by
// the time someone asks about a function.
const R_HELPER_WARM_MS = 1000;

/** Metadata key of a complete/inspect reply: 'helper' when a helper R process answered it while the session was busy (see r-helper.ts). */
export const ANSWERED_BY = 'jovian/answered-by';
/** Metadata key of a complete/inspect reply: true when it was not answered because a cell is running and `waitForCell` was false. */
export const KERNEL_BUSY = 'jovian/busy';

// The requests each kernel answers itself while a cell runs (the native
// kernels' Interpreter::answersWhileBusy()); others wait for the cell.
const ANSWERED_WHILE_BUSY: Record<string, readonly string[]> = {
    r: [],
    python: ['complete_request', 'inspect_request', 'is_complete_request'],
    stata: ['is_complete_request'],
    ark: []
};

// How long stop() waits for the kernel's shutdown_reply after the
// supervisor reports the stop done (it is normally already here by then).
const SHUTDOWN_REPLY_WAIT_MS = 250;

function streamLength(message: JupyterMessage): number {
    const text = (message.content as { text?: unknown } | undefined)?.text;
    return typeof text === 'string' ? text.length : 0;
}

function replyTypeOf(requestType: string): string {
    return requestType.replace(/_request$/, '_reply');
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

// Per entry: an execution that prints millions of lines must not make
// getHistory() (and anything that serializes it, e.g. a browser reloading its
// transcript) hold and ship hundreds of megabytes. The newest stream text is
// kept.
const MAX_HISTORY_STREAM_CHARS = 500_000;

/** R code calling one of the kernel's RPCs (.jv.rpc.call() in tools:jovian) with its arguments as JSON: a JSON string is a valid R string. */
function rpcCode(method: string, args: Record<string, unknown>): string {
    return `base::as.environment("tools:jovian")$.jv.rpc.call(${JSON.stringify(method)}, ${JSON.stringify(JSON.stringify(args))})`;
}

/** SessionManager.ensureRPackage(), as a session gets it. */
type EnsureR = (request: EnsureRPackageRequest, options: EnsureRPackageOptions) => Promise<RPackageResult>;

export class Session extends EventEmitter {
    private debugSeq = 0;
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
    private currentOptions: EngineOptions;
    private readonly supervisor: SupervisorClient;
    private readonly logger: Logger;
    private readonly router: MessageRouter;
    private readonly middleware: MiddlewareChain;
    private readonly queue: ExecutionQueue;
    /**
     * The options this session is currently running with: what it was
     * created with, updated by any `restart(options)` that changed them.
     */
    get options(): EngineOptions {
        return this.currentOptions;
    }

    /** Whether the session was stopped, or its kernel ended for good. */
    get isStopped(): boolean {
        return this.stopped || this.dead !== undefined;
    }

    private readyPromise: Promise<void>;
    private stopped = false;
    // Why the kernel is gone, when it ended without stop(): its process exited, or the connection to the supervisor
    // was lost. Nothing sent to the session is answered from then on, until restart() starts a new kernel.
    private dead: string | undefined;
    private readonly comms = new Map<string, Comm>();
    private readonly busyRequests = new Set<string>();
    private readonly pendingRequests = new Map<string, PendingRequest>();
    private kernelExecutionState: ExecutionState | undefined;

    // R sessions only: the helper that answers completion/inspection while a
    // cell runs (from SessionManager, one per R installation), what this
    // session had attached and defined when it last finished a cell, and the
    // packages the cells now running attach themselves.
    private readonly rHelperFor: ((options: EngineOptions) => RHelper | undefined) | undefined;
    // The session manager's R package installer (SessionManager.ensureRPackage()), for installPackages()
    private readonly ensureR: EnsureR | undefined;
    private rState: RSessionState = { packages: [], globals: [] };
    // What the R cells now running attach and define, read from their code
    // (r-static.ts): they report it themselves only when they finish.
    private readonly runningFacts = new Map<symbol, RCodeFacts>();

    // Every execute() call's code + the iopub messages it produced, bucketed
    // by the execute_request's own msg id (execute_input's parentMsgId) --
    // see getHistory()'s doc comment for what this is actually for.
    private readonly executionHistory: ExecutionHistoryEntry[] = [];
    private readonly executionHistoryByMsgId = new Map<string, ExecutionHistoryEntry>();
    private readonly historyStreamChars = new WeakMap<ExecutionHistoryEntry, number>();

    constructor(
        info: SessionConnectionInfo,
        options: EngineOptions,
        supervisor: SupervisorClient,
        logging: { level?: LogThreshold | undefined; logger?: LoggerFunction | undefined } = {},
        rHelperFor?: (options: EngineOptions) => RHelper | undefined,
        ensureR?: EnsureR
    ) {
        super();
        this.info = info;
        this.currentOptions = options;
        this.supervisor = supervisor;
        this.rHelperFor = rHelperFor;
        this.ensureR = ensureR;
        this.logger = new Logger(options.logger ?? logging.logger, logging.level);
        this.on('message', (message: JupyterMessage) => {
            this.recordExecutionHistory(message);
            this.settleRequest(message);
            this.trackExecutionState(message);
            this.routeComm(message);
        });

        this.router = new MessageRouter(this);
        this.router.registerHandler('stream', new StreamHandler());
        this.router.registerHandler('execute_result', new ResultHandler());
        this.router.registerHandler('display_data', new DisplayHandler());
        this.router.registerHandler('error', new ErrorHandler());
        // a question of the R code to the host's UI (rstudioapi): 'ui', not 'input_request' (see UiRequest)
        this.router.registerHandler('input_request', {
            handle: async (message: JupyterMessage) => {
                const content = message.content as { prompt?: string; password?: boolean; jovian_ui?: { method?: string; params?: Record<string, unknown> } };
                if (!content?.jovian_ui) {
                    return false; // an ordinary prompt (readline()): the 'input_request' event
                }
                let answered = false;
                const reply = (answer: unknown) => {
                    if (answered) return;
                    answered = true;
                    this.sendInputReply(answer === undefined || answer === null ? '' : JSON.stringify(answer));
                };
                if (this.listenerCount('ui') === 0) {
                    reply(null);
                    return true;
                }
                const request: UiRequest = { method: content.jovian_ui.method ?? '', params: content.jovian_ui.params ?? {}, reply };
                this.emit('ui', request);
                return true;
            }
        });

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
        // answered (or given up on, after its own timeout) before the queue sends the next cell
        this.queue = new ExecutionQueue(wsAddon, this, options.queueSize, this.logger, () => this.interrupt());

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
            this.logger.debug(`Connecting to session ${this.info.sessionId} at ${this.info.wsBase}`);
            const ws = new WebSocket(sessionSocketUrl(this.info));
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
                // The supervisor sends what it has queued as one WebSocket
                // frame, the JSON frames separated by '\n' (see
                // native/src/themisto/outbox.hpp); JSON text never contains
                // a raw newline.
                for (const text of String(event.data).split('\n')) {
                    if (text) void this.handleFrame(text, onReady);
                }
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
                this.dead = 'the connection to the supervisor was lost';
                this.emit('exit', { reason: 'WebSocket connection to the supervisor closed unexpectedly' });
                this.queue.clear();
                this.closeAllComms('connection lost');
                this.rejectPendingRequests(new Error('WebSocket connection to the supervisor closed unexpectedly'));
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
     * with -- e.g. flip from R 4.4 to R 4.6 on the fly, without closing
     * this session and opening a new one (a different session id/WS URL)
     * just to pick a different R.
     */
    async restart(options?: Partial<EngineOptions>): Promise<void> {
        if (this.stopped) {
            throw new Error(`Cannot restart session ${this.info.sessionId}: it was already stopped`);
        }

        this.logger.info(`Restarting session ${this.info.sessionId}`);
        this.queue.clear();
        this.closeAllComms('kernel restarted');
        // The supervisor replaces a session's options wholesale, so send the
        // merge -- a restart that only switches rHome must keep the
        // workingDirectory, rLibs, ... the session was created with.
        const merged = options ? { ...this.currentOptions, ...withAbsolutePaths(options) } : undefined;

        // Reassigned synchronously, before awaiting anything below, so a
        // concurrent execute()/createShiny() call that reads this.readyPromise
        // while the restart is still in flight waits for the new connection
        // instead of racing the old (already-dead-or-dying) one.
        this.rejectPendingRequests(new Error('Session is restarting'));
        this.dead = undefined;
        const shutdownReply = this.watchFor('shutdown_reply');
        this.readyPromise = (async () => {
            try {
                const mergedOptions = merged ? await withDiscoveredRuntime(merged) : undefined;
                await this.supervisor.restartSession(this.info, mergedOptions);
                if (mergedOptions) {
                    this.currentOptions = mergedOptions;
                }
            } finally {
                // The supervisor sent the old kernel a real shutdown_request
                // (restart: true); its shutdown_reply arrives on this (old)
                // socket as a normal 'shutdown_reply' event -- give it a
                // moment to land before that socket is replaced. (A kernel
                // that had already crashed never sends one, hence the short
                // cap.) In a finally so the watcher is always cleaned up.
                await shutdownReply(200);
            }
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
            if (message.msgType === 'stream') {
                this.boundStreamHistory(entry, streamLength(message));
            }
        }
    }

    // Drops the oldest stream messages of `entry` until it holds at most
    // MAX_HISTORY_STREAM_CHARS of stream text.
    private boundStreamHistory(entry: ExecutionHistoryEntry, added: number): void {
        let total = (this.historyStreamChars.get(entry) ?? 0) + added;
        while (total > MAX_HISTORY_STREAM_CHARS) {
            const index = entry.messages.findIndex((m) => m.msgType === 'stream');
            // Keep at least the message that was just added.
            if (index < 0 || index === entry.messages.length - 1) break;
            total -= streamLength(entry.messages[index]);
            entry.messages.splice(index, 1);
            entry.truncated = true;
        }
        this.historyStreamChars.set(entry, total);
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
                    // with what the kernel printed as it went down (e.g. "[elara] FATAL: bad allocation"), not only its exit code
                    const reason = this.supervisor.describeKernelExit(typeof frame.reason === 'string' ? frame.reason : 'unknown reason');
                    this.logger.error(`R session process for ${this.info.sessionId} exited unexpectedly: ${reason}`);
                    this.dead = reason;
                    this.emit('exit', { reason });
                    this.queue.clear();
                    this.closeAllComms('kernel exited');
                    this.rejectPendingRequests(new Error(`Session process exited: ${reason}`));
                }
                break;

            case 'requestError': {
                // The supervisor refused a request outright (unknown type
                // for that channel, session gone) -- no reply will ever come.
                const id = typeof frame.id === 'string' ? frame.id : '';
                const error = new Error(typeof frame.error === 'string' ? frame.error : 'request rejected by the supervisor');
                const pending = this.pendingRequests.get(id);
                if (pending) {
                    clearTimeout(pending.timer);
                    this.pendingRequests.delete(id);
                    pending.reject(error);
                } else {
                    // Fire-and-forget (comm_*): nothing awaiting it.
                    this.emit('requestError', { id, error });
                }
                break;
            }

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
        // a kernel that has ended answers nothing: said at once, not after the execution's timeout
        if (this.dead !== undefined) {
            throw new Error(`The session's kernel has ended (${this.dead}); restart() starts a new one`);
        }
        const helper = this.busyHelper();
        if (!helper) {
            return this.queue.execute(code, options);
        }

        // An R cell also reports, at its end, what the session has attached
        // and defined (one more user expression, removed from the result) --
        // what the helper needs to answer for this session while a later
        // cell runs.
        const running = Symbol('execution');
        this.runningFacts.set(running, analyzeR(code));
        const warm = setTimeout(() => helper.warm(this.busyState()), R_HELPER_WARM_MS);
        try {
            const result = await this.queue.execute(code, {
                ...options,
                userExpressions: { ...options.userExpressions, [R_STATE_KEY]: R_STATE_EXPRESSION }
            });
            const reported = result.userExpressions?.[R_STATE_KEY];
            if (result.userExpressions) {
                delete result.userExpressions[R_STATE_KEY];
                if (Object.keys(result.userExpressions).length === 0) delete result.userExpressions;
            }
            this.rState = parseRState(reported) ?? this.rState;
            return result;
        } finally {
            clearTimeout(warm);
            this.runningFacts.delete(running);
        }
    }

    // The helper for this session when it is an R session (none otherwise).
    private busyHelper(): RHelper | undefined {
        if ((this.currentOptions.kernelType ?? 'r') !== 'r') return undefined;
        return this.rHelperFor?.(this.currentOptions);
    }

    // What the helper should have attached: the last reported state plus
    // what the running cells attach themselves.
    private busyState(): RSessionState {
        const packages = new Set(this.rState.packages);
        const globals = new Set(this.rState.globals);
        for (const facts of this.runningFacts.values()) {
            facts.packages.forEach((p) => packages.add(p));
            facts.defines.forEach((n) => globals.add(n));
        }
        return { packages: [...packages], globals: [...globals] };
    }

    /**
     * The R packages this session has loaded (loadedNamespaces(): their DLLs are in use), asked of it now; undefined
     * when that can't be known -- code is running (it may load anything), or the kernel doesn't say (not Elara).
     * Asking never interrupts: the kernel may be running a cell this client did not send (one left running after its
     * timeout, or started before this client attached), and a question that waits behind it only gives up.
     */
    async loadedRPackages(): Promise<string[] | undefined> {
        if ((this.currentOptions.kernelType ?? 'r') !== 'r' || this.queue.busy || this.isStopped || this.executionState === 'busy') return undefined;
        try {
            const result = await this.queue.execute('', {
                silent: true,
                storeHistory: false,
                userExpressions: { [R_STATE_KEY]: R_STATE_EXPRESSION },
                timeout: 10_000,
                interruptOnTimeout: false
            });
            return parseRState(result.userExpressions?.[R_STATE_KEY])?.loaded;
        } catch {
            return undefined;
        }
    }

    // Whether `msgType` sent now would wait for a running cell.
    private waitsForCell(msgType: string): boolean {
        return this.queue.busy && !(ANSWERED_WHILE_BUSY[this.currentOptions.kernelType ?? 'r'] ?? []).includes(msgType);
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
        // Wire field names are snake_case (Jupyter's history_request).
        const content: Record<string, unknown> = {
            hist_access_type: options.histAccessType ?? 'tail',
            output: options.output ?? false,
            raw: options.raw ?? true,
            n: options.n ?? 100
        };
        for (const key of ['session', 'start', 'stop', 'pattern', 'unique'] as const) {
            if (options[key] !== undefined) content[key] = options[key];
        }
        const reply = await this.request<{ history?: KernelHistoryEntry[] }>('history_request', content);
        return reply.history ?? [];
    }

    /**
     * Sends interrupt_request over the control channel and resolves true if
     * the kernel acknowledged it (interrupt_reply, status ok), false if it
     * didn't within `options.timeout` (default 5s) or the session is gone --
     * never rejects, since a caller typically fires this from a "stop"
     * button and has nothing useful to do with an error.
     *
     * A real interrupt: the kernel services its control channel on a
     * separate thread while code runs, so this is answered immediately even
     * mid-execution, and the running code is broken out of exactly as Ctrl-C
     * would (R: an interrupt condition; Python: KeyboardInterrupt). The
     * interrupted execute() resolves with success: false. Interrupting an
     * idle kernel does nothing. Limits: code blocked inside a native call
     * that never returns to the interpreter (a long C extension call, a
     * blocking socket read) is only interrupted once it does, and a kernel
     * waiting on an input() / readline() reply must be answered (or its
     * execute() timed out) first.
     */
    async interrupt(options: { timeout?: number | undefined } = {}): Promise<boolean> {
        try {
            const reply = await this.request<InterruptReplyContent>('interrupt_request', {}, { timeout: options.timeout ?? 5000 });
            return reply.status === 'ok';
        } catch {
            return false;
        }
    }

    /**
     * The latest iopub `status` the kernel reported ('busy' while it is
     * handling a request, 'idle' between them) -- undefined until the first
     * one arrives. Also available as the 'status' event.
     */
    get executionState(): ExecutionState | undefined {
        return this.kernelExecutionState;
    }

    /**
     * Sends any of the Jupyter requests that have a plain request/reply
     * shape and resolves with the kernel's reply content: complete_request,
     * inspect_request, is_complete_request, kernel_info_request,
     * history_request, comm_info_request (shell) and interrupt_request
     * (control) -- the typed methods below (complete(), inspect(), ...) are
     * this with the right message type and content filled in. Rejects on a
     * reply whose status is 'error'/'aborted', when the supervisor refuses
     * the request, on timeout, or if the session goes away first.
     *
     * Deliberately not for execute_request (use execute(): it owns the
     * queue/timeout/stdin semantics), input_reply (sendInputReply()) or
     * shutdown_request (stop()/restart()) -- the supervisor rejects those.
     */
    async request<T = any>(msgType: string, content: Record<string, unknown> = {}, options: { timeout?: number | undefined } = {}): Promise<T> {
        await this.readyPromise;

        const id = randomUUID();
        const timeoutMs = options.timeout ?? DEFAULT_REQUEST_TIMEOUT_MS;
        const channel = msgType === 'interrupt_request' || msgType === 'debug_request' ? 'control' : 'shell';
        const replyType = replyTypeOf(msgType);

        return new Promise<T>((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pendingRequests.delete(id);
                reject(new Error(`Timed out waiting for a ${replyType} after ${timeoutMs}ms`));
            }, timeoutMs);

            this.pendingRequests.set(id, {
                replyType,
                resolve: resolve as (content: unknown) => void,
                reject,
                timer
            });

            this.send({ type: 'request', id, channel, msgType, content });
        });
    }

    // busy/idle come in pairs per request the kernel handles -- and an
    // interrupt is handled WHILE an execution is running, so its idle must
    // not flip the state to idle under a still-running execution. Busy while
    // any request is outstanding.
    private trackExecutionState(message: JupyterMessage): void {
        if (message.msgType !== 'status') {
            return;
        }
        const state = (message.content as { execution_state?: ExecutionState })?.execution_state;
        if (state === 'busy') {
            this.busyRequests.add(message.parentMsgId);
            this.kernelExecutionState = 'busy';
        } else if (state === 'idle') {
            this.busyRequests.delete(message.parentMsgId);
            this.kernelExecutionState = this.busyRequests.size > 0 ? 'busy' : 'idle';
        } else {
            this.kernelExecutionState = state;
        }
    }

    // Routes the kernel's comm traffic to the Comm objects.
    private routeComm(message: JupyterMessage): void {
        const content = message.content as { comm_id?: string; target_name?: string; data?: Record<string, unknown> } | undefined;
        const commId = content?.comm_id;
        if (!commId) {
            return;
        }
        switch (message.msgType) {
            case 'comm_open': {
                if (this.comms.has(commId)) {
                    return;
                }
                const comm = new Comm(commId, content?.target_name ?? '', this);
                this.comms.set(commId, comm);
                if (content?.target_name === 'jovian.ui') {
                    // the R kernel's notifications to the host's UI (rstudioapi): 'ui' events (see UiRequest)
                    comm.on('message', (data: { method?: string; params?: Record<string, unknown> }) => {
                        const request: UiRequest = { method: data?.method ?? '', params: data?.params ?? {} };
                        this.emit('ui', request);
                    });
                    break;
                }
                this.emit('comm', comm, content?.data ?? {});
                break;
            }
            case 'comm_msg':
                this.comms.get(commId)?.receiveMessage(content?.data ?? {});
                break;
            case 'comm_close': {
                const comm = this.comms.get(commId);
                this.comms.delete(commId);
                comm?.receiveClose(content?.data ?? {});
                break;
            }
            default:
                break;
        }
    }

    private closeAllComms(reason: string): void {
        for (const comm of this.comms.values()) {
            comm.receiveClose({ reason });
        }
        this.comms.clear();
        this.busyRequests.clear();
    }

    private settleRequest(message: JupyterMessage): void {
        const pending = this.pendingRequests.get(message.parentMsgId);
        if (!pending || message.msgType !== pending.replyType) {
            return;
        }
        clearTimeout(pending.timer);
        this.pendingRequests.delete(message.parentMsgId);

        const content = message.content as { status?: string; ename?: string; evalue?: string } | undefined;
        if (content?.status === 'error') {
            pending.reject(new Error(content.evalue ?? content.ename ?? `${pending.replyType} reported an error`));
        } else if (content?.status === 'aborted') {
            pending.reject(new Error(`${pending.replyType} was aborted: an earlier request failed with stopOnError`));
        } else {
            pending.resolve(message.content);
        }
    }

    private rejectPendingRequests(error: Error): void {
        for (const [id, pending] of this.pendingRequests) {
            clearTimeout(pending.timer);
            pending.reject(error);
            this.pendingRequests.delete(id);
        }
    }

    // Starts listening for `msgType` NOW and returns a function that waits
    // (up to its timeout) for it -- so a message that arrives before the
    // caller gets around to waiting (e.g. a shutdown_reply that lands while
    // the HTTP stop call is still in flight) is not missed. Never rejects:
    // resolves undefined on timeout.
    private watchFor(msgType: string): (timeoutMs: number) => Promise<JupyterMessage | undefined> {
        let received: JupyterMessage | undefined;
        let notify: (() => void) | undefined;
        const listener = (message: JupyterMessage) => {
            if (message.msgType !== msgType) return;
            received = message;
            this.off('message', listener);
            notify?.();
        };
        this.on('message', listener);

        return (timeoutMs: number) => new Promise((resolve) => {
            if (received) {
                resolve(received);
                return;
            }
            const timer = setTimeout(() => {
                this.off('message', listener);
                resolve(undefined);
            }, timeoutMs);
            notify = () => {
                clearTimeout(timer);
                resolve(received);
            };
        });
    }

    /**
     * What the supervisor knows about this session's kernel process right
     * now: lifecycle status, pid, memory, working directory and the
     * heartbeat (round-trip time of the last ping, missed pings). The
     * heartbeat is answered by a kernel thread separate from the one that
     * runs code, so it stays live while the kernel is busy -- unlike
     * kernelInfo(), which would wait for the running code to finish.
     */
    async status(): Promise<SessionStatusInfo> {
        const res = await fetch(`${this.info.httpBase}/sessions/${this.info.sessionId}`, { headers: supervisorHeaders(this.info.token) });
        if (!res.ok) {
            throw new Error(`Could not read the status of session ${this.info.sessionId} (HTTP ${res.status})`);
        }
        return await res.json() as SessionStatusInfo;
    }

    /**
     * complete_request: completions for the code at `cursorPos` (default: the
     * end of `code`, in code points). While an R session runs a cell, a helper
     * R process with the same packages attached answers instead (its matches
     * plus the session's own names from its last finished cell; `metadata`
     * [ANSWERED_BY] is 'helper'). See BusyRequestOptions for `waitForCell`.
     */
    async complete(code: string, cursorPos: number = code.length, options: BusyRequestOptions = {}): Promise<CompleteReplyContent> {
        const content = { code, cursor_pos: cursorPos };
        const helper = this.waitsForCell('complete_request') ? this.busyHelper() : undefined;
        if (helper) {
            try {
                const state = this.busyState();
                const reply = await helper.ask<CompleteReplyContent>('complete_request', content, state);
                const merged = mergeCompletions(reply, code, cursorPos, [...state.globals, ...analyzeR(code).defines]);
                return { ...reply, ...merged, metadata: { ...reply.metadata, [ANSWERED_BY]: 'helper' } };
            } catch (error) {
                this.logger.debug(`The R helper could not complete while the session is busy: ${(error as Error).message}`);
            }
        }
        if (options.waitForCell === false && this.waitsForCell('complete_request')) {
            return { status: 'ok', matches: [], cursor_start: cursorPos, cursor_end: cursorPos, metadata: { [KERNEL_BUSY]: true } };
        }
        const reply = await this.request<CompleteReplyContent>('complete_request', content, { timeout: options.timeout });
        // R's completer knows only what exists in the session; names defined
        // earlier in the code being typed (not run yet) come from reading it.
        if ((this.currentOptions.kernelType ?? 'r') === 'r' && reply.status === 'ok') {
            return { ...reply, ...mergeCompletions(reply, code, cursorPos, analyzeR(code).defines) };
        }
        return reply;
    }

    /**
     * inspect_request: documentation/details for the symbol at `cursorPos`
     * (default: the end of `code`). While an R session runs a cell, a helper
     * R process with the same packages attached answers for anything it can
     * find -- a package's function, its help page -- (`metadata`
     * [ANSWERED_BY] is 'helper'); something only the busy session has (an
     * object it created) still waits for the cell. See BusyRequestOptions for
     * `waitForCell`.
     */
    async inspect(code: string, cursorPos: number = code.length, detailLevel: 0 | 1 = 0, options: BusyRequestOptions = {}): Promise<InspectReplyContent> {
        const content = { code, cursor_pos: cursorPos, detail_level: detailLevel };
        const helper = this.waitsForCell('inspect_request') ? this.busyHelper() : undefined;
        if (helper) {
            try {
                const reply = await helper.ask<InspectReplyContent>('inspect_request', content, this.busyState());
                if (reply.found) return { ...reply, metadata: { ...reply.metadata, [ANSWERED_BY]: 'helper' } };
            } catch (error) {
                this.logger.debug(`The R helper could not inspect while the session is busy: ${(error as Error).message}`);
            }
        }
        if (options.waitForCell === false && this.waitsForCell('inspect_request')) {
            return { status: 'ok', found: false, data: {}, metadata: { [KERNEL_BUSY]: true } };
        }
        return this.request<InspectReplyContent>('inspect_request', content, { timeout: options.timeout });
    }

    /**
     * is_complete_request: whether `code` is a complete statement, needs more
     * lines ('incomplete', with an `indent` hint when the kernel has one),
     * or can never parse ('invalid') -- what a console needs to decide
     * between "run it" and "keep prompting".
     */
    isComplete(code: string): Promise<IsCompleteReplyContent> {
        return this.request<IsCompleteReplyContent>('is_complete_request', { code });
    }

    /** kernel_info_request: what the kernel is -- implementation, language and its version, protocol version, banner. */
    kernelInfo(): Promise<KernelInfoReplyContent> {
        return this.request<KernelInfoReplyContent>('kernel_info_request');
    }

    /** comm_info_request: the comms currently open in the kernel, optionally only those for one target. */
    // ---- what only one kernel can do: session.r (Elara), session.stata (Callisto) ------------------------------

    private rFeatures: RSession | undefined;
    private stataFeatures: StataSession | undefined;

    /** What only an R session can do -- its packages, R's help, a Shiny app -- or undefined for another kernel. */
    get r(): RSession | undefined {
        if ((this.currentOptions.kernelType ?? 'r') !== 'r') return undefined;
        return (this.rFeatures ??= new RSession(this));
    }

    /** What only a Stata session can do -- the dataset in its memory -- or undefined for another kernel. */
    get stata(): StataSession | undefined {
        if (this.currentOptions.kernelType !== 'stata') return undefined;
        return (this.stataFeatures ??= new StataSession(this));
    }

    // The installer and the log, for session.r (SessionManager gives the installer; attachSession()'s have it too)
    /** @internal */
    get installer(): EnsureR | undefined { return this.ensureR; }
    /** @internal */
    get log(): Logger { return this.logger; }

    /**
     * The Jupyter debug protocol (JEP 47): a Debug Adapter Protocol request (`command`, `args`), sent as a
     * debug_request on the control channel -- so also while a cell runs or is stopped at a breakpoint -- and its DAP
     * response. The kernel's DAP events ("stopped", "continued", ...) arrive as 'debug_event' events. R sessions
     * (Elara) have a debugger: see docs/guides/debugging.md.
     */
    async debugRequest<T = any>(command: string, args: Record<string, unknown> = {}, options: { timeout?: number | undefined } = {}): Promise<DapResponse<T>> {
        this.debugSeq += 1;
        return this.request<DapResponse<T>>('debug_request', { seq: this.debugSeq, type: 'request', command, arguments: args }, options);
    }

    // ---- the session's variables, for a variables pane and a data viewer (R: hera's variables.R; Python: Carpo) ----

    /**
     * The objects of an R session's global environment, or a Python session's `__main__` (not modules, not names
     * starting with `_`): name, type, size, a one-line preview, and whether it is a table readTable() reads. Stata
     * sessions have a dataset instead: stataDataset().
     */
    async listVariables(options: { timeout?: number | undefined } = {}): Promise<SessionVariable[]> {
        const timeout = options.timeout ?? 60_000;
        if (this.currentOptions.kernelType === 'python') {
            return this.callCarpo<SessionVariable[]>('.jovian_variables', '', timeout);
        }
        return this.rpc<SessionVariable[]>('var_list', {}, timeout);
    }

    /**
     * Rows of a table in the session -- an R data frame or matrix, a pandas DataFrame or Series, a numpy array, a
     * polars DataFrame -- as text, as the language prints them: `count` rows (default 100, at most 100 000) from
     * `start` (1, the first), with its columns and number of rows.
     */
    async readTable(name: string, options: { start?: number | undefined; count?: number | undefined; timeout?: number | undefined } = {}): Promise<TablePage> {
        const request = { name, start: options.start ?? 1, count: options.count ?? 100 };
        const timeout = options.timeout ?? 120_000;
        if (this.currentOptions.kernelType === 'python') {
            return this.callCarpo<TablePage>('.jovian_table', JSON.stringify(request), timeout);
        }
        return this.rpc<TablePage>('var_table', request, timeout);
    }

    // A request Carpo answers itself, as a user expression of a silent execution: no output, no execution count.
    private async callCarpo<T>(key: string, expression: string, timeout: number): Promise<T> {
        const result = await this.execute('', { silent: true, userExpressions: { [key]: expression }, timeout });
        const reply = result.userExpressions?.[key] as { status?: string; ename?: string; evalue?: string; data?: Record<string, unknown> } | undefined;
        if (!reply || reply.status !== 'ok') {
            throw new Error(`${key.slice(1)} failed: ${reply?.evalue || reply?.ename || 'no reply'}`);
        }
        return JSON.parse(String(reply.data?.['text/plain'] ?? 'null')) as T;
    }

    // A request Callisto answers itself, as a user expression of a silent execution: no output, no execution count.
    /** @internal */
    async stataCall<T>(key: string, expression: string, timeout: number): Promise<T> {
        if (this.currentOptions.kernelType !== 'stata') {
            throw new Error(`${key.slice(1)}: only for Stata sessions (Callisto)`);
        }
        const result = await this.execute('', { silent: true, userExpressions: { [key]: expression }, timeout });
        const reply = result.userExpressions?.[key] as { status?: string; evalue?: string; data?: Record<string, unknown> } | undefined;
        if (!reply || reply.status !== 'ok') {
            throw new Error(`${key.slice(1)} failed: ${reply?.evalue ?? 'no reply'}`);
        }
        return JSON.parse(String(reply.data?.['text/plain'] ?? 'null')) as T;
    }

    // A hera RPC, answered as a user expression of a silent execution: no output, no execution count.
    /** @internal */
    async rpc<T>(method: string, args: Record<string, unknown>, timeout: number): Promise<T> {
        if ((this.currentOptions.kernelType ?? 'r') !== 'r') {
            throw new Error(`${method}: only for R sessions (Elara)`);
        }
        const key = '.jovian_rpc';
        const result = await this.execute('', { silent: true, userExpressions: { [key]: rpcCode(method, args) }, timeout });
        const reply = result.userExpressions?.[key] as { status?: string; evalue?: string; data?: Record<string, unknown> } | undefined;
        if (!reply || reply.status !== 'ok') {
            throw new Error(`${method} failed: ${reply?.evalue ?? 'no reply'}`);
        }
        return JSON.parse(String(reply.data?.['text/plain'] ?? 'null')) as T;
    }

    commInfo(targetName?: string): Promise<CommInfoReplyContent> {
        return this.request<CommInfoReplyContent>('comm_info_request', targetName === undefined ? {} : { target_name: targetName });
    }

    // comm_open/comm_msg/comm_close have no reply -- the kernel's side of a
    // comm arrives as 'comm_open'/'comm_msg'/'comm_close' events (and a
    // comm_open for a target the kernel doesn't know is answered with a
    // comm_close whose parentMsgId is the msgId returned here). Each returns
    // the msg id it was sent under for that correlation.
    private async sendComm(msgType: string, content: Record<string, unknown>): Promise<string> {
        await this.readyPromise;
        const id = randomUUID();
        this.send({ type: 'request', id, channel: 'shell', msgType, content });
        return id;
    }

    /** Opens a comm to a kernel-side `targetName`; resolves with its comm id and the msg id it was sent under. */
    async commOpen(targetName: string, data: Record<string, unknown> = {}, commId: string = randomUUID()): Promise<{ commId: string; msgId: string }> {
        const msgId = await this.sendComm('comm_open', { comm_id: commId, target_name: targetName, data });
        return { commId, msgId };
    }

    /**
     * Opens a comm to a kernel-side `targetName` and returns it as a Comm
     * object (send()/close(), 'message'/'close' events). If the kernel has
     * no such target it answers with a comm_close, so the returned comm
     * emits 'close' shortly after. Kernel-initiated comms arrive as this
     * session's 'comm' event instead: `session.on('comm', (comm, data) => ...)`.
     */
    async openComm(targetName: string, data: Record<string, unknown> = {}): Promise<Comm> {
        const commId = randomUUID();
        const comm = new Comm(commId, targetName, this);
        // Registered before the request goes out so nothing the kernel
        // sends in reply can arrive for a comm we haven't heard of yet.
        this.comms.set(commId, comm);
        try {
            await this.commOpen(targetName, data, commId);
        } catch (error) {
            this.comms.delete(commId);
            throw error;
        }
        return comm;
    }

    /** Sends `data` over an open comm. */
    commMsg(commId: string, data: Record<string, unknown> = {}): Promise<string> {
        return this.sendComm('comm_msg', { comm_id: commId, data });
    }

    /** Closes a comm. */
    commClose(commId: string, data: Record<string, unknown> = {}): Promise<string> {
        return this.sendComm('comm_close', { comm_id: commId, data });
    }

    /** Stops the R session and waits for its process to exit. */
    async stop(): Promise<void> {
        if (this.stopped) {
            return;
        }
        this.stopped = true;
        this.logger.info(`Stopping session ${this.info.sessionId}`);
        this.queue.clear();
        this.closeAllComms('session stopped');
        this.rejectPendingRequests(new Error('Session stopped'));
        const shutdownReply = this.watchFor('shutdown_reply');
        try {
            await this.supervisor.stopSession(this.info);
        } finally {
            await shutdownReply(SHUTDOWN_REPLY_WAIT_MS);
        }
        this.ws?.close();
        this.emit('stopped');
    }

    /**
     * Closes this client's connection without stopping the kernel (used by
     * SessionManager.detach() in persistent mode). The Session is unusable
     * afterwards; SessionManager.attachSession() makes a new one.
     */
    disconnect(): Promise<void> {
        if (this.stopped) return Promise.resolve();
        this.stopped = true;
        this.queue.clear();
        this.rejectPendingRequests(new Error('Session was disconnected'));
        const ws = this.ws;
        if (!ws || ws.readyState === WebSocket.CLOSED) return Promise.resolve();
        // Resolves once the socket has really closed: a process that exits
        // with it still closing trips an assertion in Node on Windows.
        return new Promise((resolve) => {
            const done = () => { clearTimeout(timer); resolve(); };
            const timer = setTimeout(done, 2000);
            ws.addEventListener('close', done, { once: true });
            ws.close();
        });
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
            this.rejectPendingRequests(new Error('Session was killed'));
            this.ws?.close();
        }
    }
}

export class SessionManager {
    private readonly logLevel: LogThreshold;
    private readonly customLogger: LoggerFunction | undefined;
    private readonly logger: Logger;
    private readonly supervisor: SupervisorClient;
    private readonly sessions = new Set<Session>();
    // ensureRPackage()'s packages sessions, by R and libraries: kept warm between installs, stopped when idle
    // (inUse: the installs that have it now -- it is idle, and its timer runs, only at none)
    private readonly packagesSessions = new Map<string, { session: Promise<Session>; inUse: number; idle?: ReturnType<typeof setTimeout> }>();
    // The R libraries and Python environments of the sessions being started: they use them from the moment they are
    // asked for, not only once the supervisor has started them
    private readonly starting = new Map<symbol, string>();
    // R libraries and Python environments an install is replacing packages in: a new session on one waits for it
    private readonly installHolds = new Map<string, Promise<void>>();
    private exitHandlerRegistered = false;
    private readonly busyHelperEnabled: boolean;
    // One helper R process per R installation (see r-helper.ts).
    private readonly rHelpers = new Map<string, RHelper>();

    /**
     * Quiet by default: only one-time setup notices, warnings and errors are
     * printed. See SessionManagerOptions for `logLevel`, `logger` and
     * `kernelOutput` (and the JOVIAN_LOG_LEVEL / JOVIAN_KERNEL_OUTPUT variables).
     */
    constructor(options: SessionManagerOptions = {}) {
        this.logLevel = options.logLevel ?? defaultLogLevel();
        this.customLogger = options.logger;
        this.logger = new Logger(this.customLogger, this.logLevel);
        const verbose = this.logLevel === 'trace' || this.logLevel === 'debug';
        const forwardKernelOutput = options.kernelOutput ?? (Boolean(process.env.JOVIAN_KERNEL_OUTPUT) || verbose);
        const persistent = options.persistent === true ? {} : options.persistent || undefined;
        this.supervisor = new SupervisorClient(this.logger, {
            forwardKernelOutput,
            persistent: persistent && {
                stateFile: persistent.stateFile ?? joinPath(homedir(), '.jovian', 'supervisor.json'),
                idleShutdownMinutes: persistent.idleShutdownMinutes ?? 60
            }
        });
        this.busyHelperEnabled = options.busyHelper ?? true;
    }

    /**
     * Every session in the supervisor -- with `persistent`, including those a
     * previous process created (a window reload, a restart of the host app).
     * Starts the supervisor if none is running.
     */
    async listSessions(): Promise<SupervisorSessionInfo[]> {
        return this.supervisor.listSessions();
    }

    /**
     * A Session for one that already exists in the supervisor (from
     * listSessions()): reconnects to its kernel, which kept running. Its
     * options are the ones it was created with. What it printed while no
     * client was connected is not replayed; its kernel-side history is
     * (queryKernelHistory()).
     */
    async attachSession(sessionId: string): Promise<Session> {
        const existing = [...this.sessions].find((s) => s.info.sessionId === sessionId);
        if (existing) return existing;

        const listed = (await this.supervisor.listSessions()).find((s) => s.sessionId === sessionId);
        if (!listed) {
            throw new Error(`The supervisor has no session ${sessionId}`);
        }
        const options: EngineOptions = Object.fromEntries(
            Object.entries(listed.options ?? { kernelType: listed.kernelType }).filter(([, value]) => value !== '' && value !== undefined)
        );
        const info = await this.supervisor.connectionFor(sessionId);
        const session = new Session(info, options, this.supervisor, { level: this.logLevel, logger: this.customLogger },
            (current) => this.rHelperFor(current), (request, options) => this.ensureRPackage(request, options));
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

    /**
     * Lets go of every session without stopping them (persistent mode): the
     * connections close, the kernels keep running, and a later
     * SessionManager can attachSession() to them. Without `persistent` the
     * supervisor ends with this process anyway, so this is stopAll().
     */
    async detach(): Promise<void> {
        if (!this.supervisor.isPersistent) {
            await this.stopAll();
            return;
        }
        // The manager's own sessions -- the packages sessions of its installs and the helper R processes -- are
        // nobody's to attach to later: stopped, not left running in the supervisor.
        const packages = [...this.packagesSessions.values()];
        this.packagesSessions.clear();
        await Promise.all(packages.map(async (entry) => {
            if (entry.idle) clearTimeout(entry.idle);
            const session = await entry.session.catch(() => undefined);
            await session?.stop().catch(() => undefined);
        }));
        await Promise.all([...this.rHelpers.values()].map((helper) => helper.stop()));
        this.rHelpers.clear();
        await Promise.all([...this.sessions].filter((session) => !session.isStopped).map((session) => session.disconnect()));
        this.sessions.clear();
    }

    /** Creates a new R session in its own OS process and waits for it to be ready. */
    async createSession(requested: EngineOptions = {}): Promise<Session> {
        // Paths made absolute against this process's working directory, then
        // R / Python / Stata found when rHome / pythonHome / stataHome were not
        // given (see runtimes.ts). Nothing is installed first: the R kernel
        // carries its own R code (hera) and needs no R package.
        const options = await withDiscoveredRuntime(withAbsolutePaths(requested));
        // an R session doesn't start on a library, nor a Python session on an environment, while an install replaces its packages
        const library = firstLibrary(options) ?? sessionVenv(options);
        // (looked at again after each wait: another install may have taken the library meanwhile)
        for (let hold = library && this.installHolds.get(library); hold; hold = this.installHolds.get(library!)) {
            this.logger.info(`Waiting for the install into ${library} to finish before starting the session`);
            await hold;
        }
        // Using the library from here on, with nothing awaited since the hold was looked at: an install that looks at
        // who uses a library after holding it sees this session, though the supervisor has yet to start it.
        const starting = Symbol('starting');
        if (library) this.starting.set(starting, library);
        let session: Session;
        try {
            const info = await this.supervisor.createSession(options);
            session = new Session(info, options, this.supervisor, { level: this.logLevel, logger: this.customLogger },
                // the packages session has no helper: nothing is asked of it while it installs
                (current) => this.packagesSessionIds.has(info.sessionId) ? undefined : this.rHelperFor(current),
                (request, options) => this.ensureRPackage(request, options));
            this.sessions.add(session);
        } finally {
            this.starting.delete(starting);
        }
        this.registerExitHandler();

        try {
            await session.ready();
        } catch (error) {
            this.sessions.delete(session);
            throw error;
        }

        return session;
    }

    /**
     * Installs or updates an R package, and what its dependency tree needs, into the first of `options.libraries`.
     * The work runs in a packages session of this manager's -- an R session of its own, kept warm between installs and
     * stopped after a few idle minutes -- never in a caller's session. An update that would replace packages waits
     * while this manager's other sessions use the library, and they start only once it is done. See r-packages.ts.
     */
    ensureRPackage(request: EnsureRPackageRequest, options: EnsureRPackageOptions): Promise<RPackageResult> {
        return ensureRPackageIn(this.packagesHost(), request, options);
    }

    /**
     * Installs what a Python app needs into the virtual environment `venvDir` (made by ensurePythonEnvironment()), with
     * pip in a process of its own. An install that would replace packages already installed waits while this manager's
     * Python sessions use the environment (their `venvPath`), and new ones on it start only once it is done. With no
     * session on it, pip isn't asked first what it would replace: new sessions wait for the whole install. See
     * python-packages.ts.
     */
    ensurePythonPackages(venvDir: string, request: PythonPackageRequest, options: PythonPackageOptions = {}): Promise<PythonPackageResult> {
        return ensurePythonPackagesIn(this.pythonHost(), venvDir, request, options);
    }

    /**
     * Makes the virtual environment `venvDir` with this Python when it is missing, or re-makes it when it was made from
     * another (see ensurePythonEnvironment()). Re-making empties it: it first waits while this manager's Python sessions
     * use it, and new ones on it start only once it is made.
     */
    ensurePythonEnvironment(pythonExecutable: string, venvDir: string, onOutput?: (line: string) => void): Promise<void> {
        return ensurePythonEnvironmentIn(this.pythonHost(), pythonExecutable, venvDir, onOutput);
    }

    private pythonHost(): PythonPackagesHost {
        return {
            sessionsUsing: async (venv) => {
                const key = libraryKey(venv);
                return [
                    ...[...this.sessions]
                        .filter((session) => !session.isStopped && sessionVenv(session.options) === key)
                        .map((session) => session.info.sessionId),
                    ...this.startingOn(key)
                ];
            },
            hold: (venv) => this.holdInstall(venv)
        };
    }

    // New sessions on the library or environment wait from now until the function returned is called
    private holdInstall(path: string): () => void {
        const key = libraryKey(path);
        let release!: () => void;
        const held = new Promise<void>((resolve) => { release = resolve; });
        this.installHolds.set(key, held);
        return () => {
            if (this.installHolds.get(key) === held) this.installHolds.delete(key);
            release();
        };
    }

    // The sessions being started on a library or environment (see createSession()), as sessionsUsing() names them
    private startingOn(key: string): string[] {
        return [...this.starting.values()].filter((library) => library === key).map(() => 'a session that is starting');
    }

    private packagesHost(): RPackagesHost {
        return {
            packagesSession: async (rHome, libraries) => {
                const key = JSON.stringify([rHome, ...libraries]);
                let entry = this.packagesSessions.get(key);
                if (!entry) {
                    const session = this.createSession({ kernelType: 'r', rHome, rLibs: libraries.join(delimiter), workingDirectory: tmpdir() })
                        .then((created) => {
                            // its errors are reported through ensureRPackage()'s result, not as the manager's
                            created.on('error', (error: unknown) => this.logger.debug('packages session error', error));
                            return created;
                        });
                    entry = { session, inUse: 0 };
                    this.packagesSessions.set(key, entry);
                    session.catch(() => this.packagesSessions.delete(key));
                }
                const current = entry;
                const session = await current.session;
                this.packagesSessionIds.add(session.info.sessionId);
                if (session.isStopped) {
                    this.packagesSessions.delete(key);
                    return this.packagesHost().packagesSession(rHome, libraries);
                }
                // in use from here until released(): it is not idle, however long the install takes
                if (current.idle) clearTimeout(current.idle);
                current.idle = undefined;
                current.inUse++;
                return session;
            },
            released: (rHome, libraries) => {
                const key = JSON.stringify([rHome, ...libraries]);
                const current = this.packagesSessions.get(key);
                if (!current || --current.inUse > 0) return;
                current.inUse = 0;
                // stopped after five minutes with no install using it
                if (current.idle) clearTimeout(current.idle);
                current.idle = setTimeout(() => {
                    if (current.inUse > 0) return;
                    if (this.packagesSessions.get(key) === current) this.packagesSessions.delete(key);
                    void current.session.then((session) => session.stop().then(() => this.sessions.delete(session))).catch(() => undefined);
                }, PACKAGES_SESSION_IDLE_MS);
                current.idle.unref?.();
            },
            sessionsUsing: async (library, packages) => {
                const key = libraryKey(library);
                const onLibrary = [...this.sessions]
                    .filter((session) => !session.isStopped && !this.packagesSessionIds.has(session.info.sessionId) && firstLibrary(session.options) === key);
                // asked now, of each: what a session loaded is what can't be replaced under it (not knowing counts as yes)
                const loaded = await Promise.all(onLibrary.map((session) => session.loadedRPackages()));
                return [
                    ...onLibrary
                        .filter((_session, i) => !loaded[i] || loaded[i]!.some((name) => packages.includes(name)))
                        .map((session) => session.info.sessionId),
                    // one that is starting may load anything
                    ...this.startingOn(key)
                ];
            },
            hold: (library) => this.holdInstall(library),
            stopHelpers: async (library) => {
                // The helper R processes of the sessions on this library have their packages attached, and are not
                // sessions: stopped here (the library is held, so none starts before the install is over), they
                // start again when a busy session is next asked something.
                const key = libraryKey(library);
                const helpers = [...this.rHelpers.entries()]
                    .filter(([options]) => firstLibrary({ kernelType: 'r', rLibs: (JSON.parse(options) as string[])[2] }) === key)
                    .map(([, helper]) => helper);
                await Promise.all(helpers.map((helper) => helper.stop().catch(() => undefined)));
            }
        };
    }

    // The ids of ensureRPackage()'s packages sessions (not "other sessions using the library")
    private readonly packagesSessionIds = new Set<string>();

    /** Gracefully stops every session managed by this instance. */
    async stopAll(): Promise<void> {
        for (const entry of this.packagesSessions.values()) {
            if (entry.idle) clearTimeout(entry.idle);
        }
        this.packagesSessions.clear();
        await Promise.all([
            ...[...this.sessions].map((session) => session.stop()),
            ...[...this.rHelpers.values()].map((helper) => helper.stop())
        ]);
        this.sessions.clear();
        this.rHelpers.clear();
        // Persistent: the supervisor is asked to stop (with any session another
        // process left in it) and forgotten.
        await this.supervisor.shutdown();
    }

    // The helper R process for sessions of this R installation (created on
    // first use; it starts its own process only when first asked).
    private rHelperFor(options: EngineOptions): RHelper | undefined {
        if (!this.busyHelperEnabled) return undefined;
        const key = JSON.stringify([options.rHome ?? '', options.rPath ?? '', options.rLibs ?? '']);
        let helper = this.rHelpers.get(key);
        if (!helper) {
            helper = new RHelper(async () => {
                // The options are the session's, already resolved (absolute,
                // discovered): same R, same libraries, same hera.
                const helperOptions: EngineOptions = {
                    kernelType: 'r',
                    rHome: options.rHome,
                    rPath: options.rPath,
                    rLibs: options.rLibs,
                    pandocPath: options.pandocPath
                };
                // not while an install replaces the packages of its library: it attaches them
                const library = firstLibrary(helperOptions);
                for (let hold = library && this.installHolds.get(library); hold; hold = this.installHolds.get(library!)) {
                    await hold;
                }
                this.logger.debug('Starting a helper R process to answer while R sessions are busy');
                const info = await this.supervisor.createSession(helperOptions);
                const session = new Session(info, helperOptions, this.supervisor, { level: this.logLevel, logger: this.customLogger });
                session.on('error', () => { });
                await session.ready();
                return session;
            });
            this.rHelpers.set(key, helper);
        }
        return helper;
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
        for (const helper of this.rHelpers.values()) helper.kill();
        this.sessions.clear();
        this.rHelpers.clear();
        this.supervisor.kill();
    }

    // Safety net: if the parent process exits (including via Ctrl+C) without
    // an explicit stopAll(), don't leave the supervisor and its spawned
    // kernel processes orphaned in the background.
    private registerExitHandler(): void {
        if (this.exitHandlerRegistered) return;
        this.exitHandlerRegistered = true;
        process.once('exit', () => {
            for (const helper of this.rHelpers.values()) helper.kill();
            // Persistent: the sessions are meant to outlive this process --
            // only this process's connections to them close.
            if (this.supervisor.isPersistent) {
                for (const session of this.sessions) void session.disconnect();
                return;
            }
            for (const session of this.sessions) session.kill();
            this.supervisor.kill();
        });
    }
}

