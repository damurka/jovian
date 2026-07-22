import { EventEmitter } from 'events';
import type { 
    EngineOptions, 
    EngineState, 
    ExecutionOptions,
    ExecutionResult 
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
