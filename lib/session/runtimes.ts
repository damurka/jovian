import { execFileSync } from 'node:child_process';
import type { EngineOptions } from '../types/index.js';

/** Runs a command and returns its last non-empty stdout line, or undefined if it failed or printed nothing. */
export type Runner = (command: string, args: string[]) => string | undefined;

export interface DiscoveryContext {
    run: Runner;
    env: Record<string, string | undefined>;
    platform: string;
}

const runCommand: Runner = (command, args) => {
    try {
        const output = execFileSync(command, args, {
            encoding: 'utf8',
            timeout: 10_000,
            stdio: ['ignore', 'pipe', 'ignore'],
            windowsHide: true
        });
        const lines = output.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
        return lines[lines.length - 1];
    } catch {
        return undefined;
    }
};

const defaultContext = (): DiscoveryContext => ({ run: runCommand, env: process.env, platform: process.platform });

/**
 * Where R lives, when the caller did not say: $R_HOME, else what `R RHOME`
 * prints (R's own answer, valid on every platform, if R is on PATH), else on
 * Windows the install path R's installer records in the registry.
 */
export function discoverRHome(context: DiscoveryContext = defaultContext()): string | undefined {
    if (context.env.R_HOME) return context.env.R_HOME;

    const fromR = context.run('R', ['RHOME']);
    if (fromR) return fromR;

    if (context.platform === 'win32') {
        for (const hive of ['HKLM', 'HKCU']) {
            const line = context.run('reg', ['query', `${hive}\\SOFTWARE\\R-core\\R`, '/v', 'InstallPath']);
            const match = line?.match(/InstallPath\s+REG_SZ\s+(.+)$/);
            if (match?.[1]) return match[1].trim();
        }
    }
    return undefined;
}

/**
 * Which Python to embed, when the caller did not say: $PYTHONHOME, else the
 * installation prefix of the first python on PATH. sys.base_prefix, not
 * sys.prefix: inside a virtual environment the latter is the venv, which has
 * no libpython to load.
 */
export function discoverPythonHome(context: DiscoveryContext = defaultContext()): string | undefined {
    if (context.env.PYTHONHOME) return context.env.PYTHONHOME;

    const script = 'import sys; print(sys.base_prefix)';
    const candidates: Array<[string, string[]]> = [
        ['python3', ['-c', script]],
        ['python', ['-c', script]]
    ];
    if (context.platform === 'win32') candidates.push(['py', ['-3', '-c', script]]);

    for (const [command, args] of candidates) {
        const home = context.run(command, args);
        if (home) return home;
    }
    return undefined;
}

/**
 * The options with rHome (R sessions) or pythonHome (Python sessions) filled
 * in when the caller left them out and the runtime could be found. Anything
 * the caller passed is kept as it is; if nothing is found the field stays
 * unset, and the kernel reports what it could not find.
 */
export function withDiscoveredRuntime(options: EngineOptions, context: DiscoveryContext = defaultContext()): EngineOptions {
    if (options.kernelType === 'python') {
        if (options.pythonHome) return options;
        const pythonHome = discoverPythonHome(context);
        return pythonHome ? { ...options, pythonHome } : options;
    }
    if (options.rHome) return options;
    const rHome = discoverRHome(context);
    return rHome ? { ...options, rHome } : options;
}
