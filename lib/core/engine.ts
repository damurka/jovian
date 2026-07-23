import { EventEmitter } from 'events';
import type {
    EngineOptions,
    EngineState,
    ExecutionOptions,
    ExecutionResult,
    ShinyAppHandle,
    ShinyAppOptions
} from '../types/index.js';
import { AddonLoader } from './addon-loader.js';
import { EnvironmentSetup } from './environment.js';
import { MessageRouter } from '../messaging/message-router.js';
import { ExecutionQueue } from '../execution/execution-queue.js';
import { MiddlewareChain } from '../middleware/middleware-chain.js';
import { LoggingMiddleware } from '../middleware/plugins/logging-plugin.js';
import { MetricsMiddleware } from '../middleware/plugins/metrics-plugin.js';
import { Logger } from '../utils/logger.js';
import { StreamHandler } from '../handlers/stream-handler.js';
import { ResultHandler } from '../handlers/result-handler.js';
import { ErrorHandler } from '../handlers/error-handler.js';
import { DisplayHandler } from '../handlers/display-handler.js';
import { findFreePort, waitForPort } from '../utils/network.js';

export class DatasuiteEngine extends EventEmitter {
    private state: EngineState = 'idle';
    private addon: any;
    private router: MessageRouter;
    private queue: ExecutionQueue;
    private middleware: MiddlewareChain;
    private logger: Logger;

    constructor(options: EngineOptions = {}) {
        super();
        
        // Initialize logger first
        this.logger = new Logger(options.logger);
        
        // Setup environment (R_HOME, PATH, etc.)
        EnvironmentSetup.configure(options, this.logger);
        
        // Load native addon
        this.addon = AddonLoader.load(options, this.logger);
        
        // Initialize message routing
        this.router = new MessageRouter(this);
        this.registerDefaultHandlers();
        
        // Initialize execution queue (listens to router 'message' events on
        // `this` to correlate replies back to the execute() call that sent them)
        this.queue = new ExecutionQueue(this.addon, this, options.queueSize);
        
        // Initialize middleware chain
        this.middleware = new MiddlewareChain();
        this.setupDefaultMiddleware(options);
    }

    async start(): Promise<void> {
        if (this.state !== 'idle') {
            throw new Error(`Cannot start engine in ${this.state} state`);
        }

        this.state = 'starting';
        this.logger.info('Starting DatasuiteEngine...');

        try {
            // Initialize C++ engine
            this.addon.init();
            
            // Start message polling
            this.addon.start((rawMessage: string) => {
                this.handleRawMessage(rawMessage);
            });
            
            this.state = 'running';
            this.emit('ready');
            this.logger.info('DatasuiteEngine started successfully');
        } catch (error) {
            this.state = 'error';
            this.logger.error('Failed to start engine', error);
            throw error;
        }
    }

    async execute(code: string, options: ExecutionOptions = {}): Promise<ExecutionResult> {
        if (this.state !== 'running') {
            throw new Error(`Engine is not running (state: ${this.state})`);
        }

        // Queue the execution and return a promise
        return this.queue.execute(code, options);
    }

    /**
     * Launches a Shiny app in this engine's R session and resolves once it's
     * actually accepting connections. `shiny::runApp()` blocks the R session
     * for as long as the app runs, so -- unlike execute() -- resolving here
     * does not mean the app is done; that's what the returned `done` promise
     * is for. Because the R session is single-threaded, no other execute()
     * call queued on this engine will run until the app stops.
     */
    async createShiny(options: ShinyAppOptions): Promise<ShinyAppHandle> {
        if (this.state !== 'running') {
            throw new Error(`Engine is not running (state: ${this.state})`);
        }

        const host = options.host ?? '127.0.0.1';
        const port = options.port ?? await findFreePort(host);
        const launchBrowser = options.launchBrowser ?? false;
        const readyTimeout = options.readyTimeout ?? 10000;

        const appDir = options.appDir.replace(/\\/g, '/').replace(/'/g, "\\'");
        const code = `shiny::runApp('${appDir}', port = ${port}, host = '${host}', launch.browser = ${launchBrowser ? 'TRUE' : 'FALSE'})`;

        // timeout: 0 -- this call is expected to block indefinitely.
        const done = this.execute(code, { timeout: 0 });

        // Race "app is listening" against "R already returned/errored", so a
        // Shiny app that fails to start (bad path, missing package, port
        // already in use, ...) surfaces immediately instead of making the
        // caller wait out the full readyTimeout. If waitForPort wins, this
        // branch is left running in the background until the app eventually
        // stops -- swallow it here so that doesn't surface as an unhandled
        // rejection; `done` itself is still returned to the caller below.
        const earlyExit = done.then((result) => {
            throw new Error(
                `Shiny app exited before it started listening (status: ${result.success ? 'ok' : 'error'})`
            );
        });
        earlyExit.catch(() => {});

        await Promise.race([waitForPort(host, port, readyTimeout), earlyExit]);

        return { host, port, url: `http://${host}:${port}`, done };
    }

    async stop(): Promise<void> {
        if (this.state === 'stopped') {
            return;
        }

        this.state = 'stopping';
        this.logger.info('Stopping DatasuiteEngine...');

        try {
            this.addon.stop();
            this.state = 'stopped';
            this.emit('stopped');
            this.logger.info('DatasuiteEngine stopped');
        } catch (error) {
            this.state = 'error';
            this.logger.error('Error stopping engine', error);
            throw error;
        }
    }

    private async handleRawMessage(rawMessage: string): Promise<void> {
        try {
            // Apply middleware chain before routing
            const processedMessage = await this.middleware.process(rawMessage);
            
            // Route to appropriate handlers
            await this.router.route(processedMessage);
        } catch (error) {
            this.logger.error('Error handling message', error);
            this.emit('error', error);
        }
    }

    private registerDefaultHandlers(): void {
        this.router.registerHandler('stream', new StreamHandler());
        this.router.registerHandler('execute_result', new ResultHandler());
        this.router.registerHandler('display_data', new DisplayHandler());
        this.router.registerHandler('error', new ErrorHandler());
    }

    private setupDefaultMiddleware(options: EngineOptions): void {
        if (options.enableLogging) {
            this.middleware.use(new LoggingMiddleware());
        }
        if (options.enableMetrics) {
            this.middleware.use(new MetricsMiddleware());
        }
    }
}
