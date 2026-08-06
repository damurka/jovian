#!/usr/bin/env node
// Writes a Jupyter kernelspec (kernel.json) for datasuite-r.exe's
// -f/--connection-file launch mode (native/src/datasuite-r.cpp) -- the
// standard "a frontend picks ports, writes a connection file, launches
// this argv with {connection_file} substituted in" protocol, as opposed
// to the datasuite-supervisor-specific --registration-port/--key mode
// used by lib/session/supervisor-client.ts.
//
// R_HOME/R_PATH are baked into argv at generation time (same
// process.env.R_HOME fallback pattern as tools/playground/server.js and
// test/integration/session-manager.test.ts) rather than looked up at
// kernel-launch time, since kernel.json's argv is static -- re-run this
// after moving R installs or rebuilding datasuite-r.exe somewhere new.
import { mkdir, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, '../..');

function defaultREnv() {
    const rHome = process.env.R_HOME || 'C:/Program Files/R/R-4.6.0';
    return {
        rHome,
        rPath: process.env.R_PATH || `${rHome}/bin/x64`,
        rLibs: process.env.R_LIBS || ''
    };
}

function resolveKernelExecutable() {
    const exeName = process.platform === 'win32' ? 'datasuite-r.exe' : 'datasuite-r';
    const candidate = path.join(REPO_ROOT, 'dist', 'native', 'Release', exeName);
    if (!existsSync(candidate)) {
        throw new Error(`datasuite-r executable not found at ${candidate} -- build it first (npm run build:native).`);
    }
    return candidate;
}

async function main() {
    const outDir = process.argv[2] || path.join(REPO_ROOT, 'kernelspec', 'datasuite-r');
    const { rHome, rPath, rLibs } = defaultREnv();
    const exePath = resolveKernelExecutable();

    const argv = [exePath, '-f', '{connection_file}', '--r-home', rHome, '--r-path', rPath];
    if (rLibs) {
        argv.push('--r-libs', rLibs);
    }

    const kernelSpec = {
        argv,
        display_name: 'R (datasuite-r)',
        language: 'R',
        interrupt_mode: 'message'
    };

    await mkdir(outDir, { recursive: true });
    const kernelJsonPath = path.join(outDir, 'kernel.json');
    await writeFile(kernelJsonPath, JSON.stringify(kernelSpec, null, 2) + '\n');

    console.log(`Wrote ${kernelJsonPath}`);
    console.log(JSON.stringify(kernelSpec, null, 2));
    console.log(`\nInstall it for the current user with:\n  jupyter kernelspec install "${outDir}" --user --name datasuite-r`);
}

main().catch((err) => {
    console.error(err.message);
    process.exit(1);
});
