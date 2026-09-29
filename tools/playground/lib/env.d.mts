import type { Installation } from './types.ts';

export function discoverRPath(rHome: string): string;
export function defaultEnvironment(): Promise<{
    rHome: string;
    rPath: string;
    rLibs: string;
    pythonHome: string;
    pythonPath: string;
    venvPath: string;
    stataHome: string;
    installations: { r: Installation[]; python: Installation[]; stata: Installation[] };
    platform: string;
    homeDirectory: string;
}>;
