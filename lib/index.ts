export { DatasuiteEngine } from './core/engine.js';
export { createKernel, executeR } from './api/datasuite.js';
export { Session, SessionManager } from './session/session-manager.js';
export type { ShinyAppHandle as SessionShinyAppHandle } from './session/session-manager.js';
export * from './types/index.js';
export * from './middleware/index.js';

// Re-export for convenience
export type {
    EngineOptions,
    ExecutionOptions,
    ExecutionResult,
    JupyterMessage,
    MessageTopic
} from './types/index.js';
