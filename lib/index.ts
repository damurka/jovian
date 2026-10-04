export { Session, SessionManager, ANSWERED_BY, KERNEL_BUSY } from './session/session-manager.js';
export { Comm } from './session/comm.js';
export type { ShinyAppHandle as SessionShinyAppHandle } from './session/session-manager.js';
export type { SupervisorSessionInfo } from './session/supervisor-client.js';
// The R, Python and Stata installations on this computer, best first (each kernel uses the first unless told which)
export { listRInstallations, listPythonInstallations, listStataInstallations, listJupyterKernels, findRuntime, readRLibraries, findRscript } from './session/runtimes.js';
export type { RuntimeInstallation, RuntimeSource, RuntimeChoice, FoundRuntime, JupyterKernel } from './session/runtimes.js';
// Installing R packages and what they need (SessionManager.ensureRPackage(), in a packages session of its own)
export { PACKAGES_IN_USE, R_PACKAGES_OFFLINE } from './session/r-packages.js';
export type { EnsureRPackageRequest, EnsureRPackageOptions, RPackageProgress, RPackageResult, WhenInUse } from './session/r-packages.js';
// Installing Python packages into a virtual environment, with pip, in a process of its own
export { ensurePythonEnvironment, ensurePythonPackages, venvPython, venvSitePackages, PYTHON_PACKAGES_OFFLINE } from './session/python-packages.js';
export type { PythonPackageRequest, PythonPackageOptions, PythonPackageResult } from './session/python-packages.js';
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
