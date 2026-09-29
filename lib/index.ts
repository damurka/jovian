export { Session, SessionManager, ANSWERED_BY, KERNEL_BUSY } from './session/session-manager.js';
export { Comm } from './session/comm.js';
export type { ShinyAppHandle as SessionShinyAppHandle } from './session/session-manager.js';
export type { SupervisorSessionInfo } from './session/supervisor-client.js';
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
