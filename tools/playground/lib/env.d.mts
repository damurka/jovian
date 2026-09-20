export function compareVersions(a: string, b: string): number;
export function looksLikeRHome(dir: string): boolean;
export function discoverRHome(): string;
export function discoverRPath(rHome: string): string;
export function defaultREnv(): { rHome: string; rPath: string; rLibs: string };
export function discoverPythonHome(): string;
export function defaultPythonEnv(): { pythonHome: string; pythonPath: string; venvPath: string };
export function defaultEnvironment(): {
    rHome: string;
    rPath: string;
    rLibs: string;
    pythonHome: string;
    pythonPath: string;
    venvPath: string;
    platform: string;
    homeDirectory: string;
};
