export { Session, SessionManager, ANSWERED_BY, KERNEL_BUSY } from './session/session-manager.js';
export { Comm } from './session/comm.js';
export type { ShinyAppHandle as SessionShinyAppHandle } from './session/session-manager.js';
export type { SupervisorSessionInfo } from './session/supervisor-client.js';
// The R, Python and Stata installations on this computer, best first (each kernel uses the first unless told which)
export { listRInstallations, listPythonInstallations, listStataInstallations, findRuntime, readRLibraries, findRscript } from './session/runtimes.js';
export type { RuntimeInstallation, RuntimeSource, RuntimeChoice, FoundRuntime } from './session/runtimes.js';
// Installing R packages and what they need, in a process of their own (not a session)
export { ensureRPackage, R_PACKAGES_OFFLINE } from './session/r-packages.js';
export type { RPackageRequest, RPackageOptions, RPackageProgress, RPackageResult } from './session/r-packages.js';
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
