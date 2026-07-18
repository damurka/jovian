import type { JupyterMessage } from './messages.js';

export type LoggerFunction = (level: 'info' | 'warn' | 'error', message: string, data?: any) => void;

export interface EngineOptions {
    rHome?: string;
    rPath?: string;
    rLibs?: string;
    queueSize?: number;
    enableLogging?: boolean;
    enableMetrics?: boolean;
    logger?: LoggerFunction;
}

export type EngineState = 
    | 'idle'
    | 'starting'
    | 'running'
    | 'stopping'
    | 'stopped'
    | 'error';

export interface ExecutionOptions {
    silent?: boolean;
    storeHistory?: boolean;
    allowStdin?: boolean;
    stopOnError?: boolean;
    timeout?: number;
}

export interface ExecutionResult {
    success: boolean;
    output: JupyterMessage[];
    error?: Error;
    executionCount?: number;
}
