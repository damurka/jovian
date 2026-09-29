#!/usr/bin/env node
// Writes Jupyter kernelspecs (kernel.json) for elara.exe's, carpo.exe's and
// callisto.exe's -f/--connection-file launch mode (native/src/elara/elara.cpp,
// native/src/carpo/carpo.cpp, native/src/callisto/callisto.cpp) -- the standard "a frontend picks ports,
// writes a connection file, launches this argv with {connection_file}
// substituted in" protocol, as opposed to the themisto-specific
// --registration-port/--key mode used by lib/session/supervisor-client.ts.
//
// Writes every kernelspec by default (elara/, carpo/ and callisto/
// kernel.json under the output directory) -- pass --only=r, --only=python or
// --only=stata to write just one. A missing executable, or no Stata found,
// only skips that one kernel's spec (with a warning), it doesn't abort the
// whole run.
//
// R_HOME/R_PATH/PYTHONHOME/STATA_HOME are baked into argv at generation time
// rather than looked up at kernel-launch time, since kernel.json's argv is
// static -- re-run this after moving an install or rebuilding the kernels
// somewhere new. They are found exactly as SessionManager finds them (the
// built library's lib/session/runtimes.ts), so run `npm run build:lib` first.
import { mkdir, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, '../..');

let runtimesModule;
async function runtimes() {
    const file = path.join(REPO_ROOT, 'dist', 'lib', 'session', 'runtimes.js');
    if (!existsSync(file)) {
        throw new Error(`The library is not built (${file} is missing) -- run \`npm run build:lib\` first.`);
    }
    runtimesModule ??= await import(pathToFileURL(file).href);
    return runtimesModule;
}

async function defaultREnv() {
    const rHome = await (await runtimes()).discoverRHome();
    // R.dll lives in bin/x64 on Windows; elsewhere R finds its own library.
    const rPath = process.env.R_PATH || (rHome && process.platform === 'win32' ? `${rHome}/bin/x64` : '');
    return { rHome, rPath, rLibs: process.env.R_LIBS || '' };
}

async function discoverPythonHome() {
    return (await runtimes()).discoverPythonHome();
}

function resolveExecutable(name) {
    const exeName = process.platform === 'win32' ? `${name}.exe` : name;
    return path.join(REPO_ROOT, 'dist', 'native', 'Release', exeName);
}

// Both elara and carpo's own base packages/extension modules need their
// interpreter's shared library findable via the dynamic linker's normal
// search path when *it* loads them later (R's utils.so/methods.so/... via
// dyn.load(); Python's own _socket/_ssl/... via its import machinery) --
// the same reason KernelProcess::start() sets this between fork() and
// exec() for the themisto-managed launch path (kernel_process.cpp). This
// is the equivalent for Jupyter's own connection-file launch mode, which
// bypasses KernelProcess entirely -- Jupyter itself spawns argv reading
// this file, so the env has to be set here, in what it spawns from.
function libDirEnv(installHome) {
    if (process.platform === 'win32') return {};
    return {
        env: {
            [process.platform === 'darwin' ? 'DYLD_LIBRARY_PATH' : 'LD_LIBRARY_PATH']: `${installHome}/lib`
        }
    };
}

async function writeElaraKernelSpec(baseDir) {
    const exePath = resolveExecutable('elara');
    if (!existsSync(exePath)) {
        console.warn(`Skipping R (Elara) kernelspec -- executable not found at ${exePath} (build it first: npm run build:native).`);
        return;
    }

    const { rHome, rPath, rLibs } = await defaultREnv();
    if (!rHome) {
        console.warn('Skipping R (Elara) kernelspec -- no R found; set R_HOME to its directory.');
        return;
    }
    const argv = [exePath, '-f', '{connection_file}', '--r-home', rHome];
    if (rPath) {
        argv.push('--r-path', rPath);
    }
    if (rLibs) {
        argv.push('--r-libs', rLibs);
    }

    const kernelSpec = {
        argv,
        display_name: 'R (Elara)',
        language: 'R',
        interrupt_mode: 'message',
        ...libDirEnv(rHome)
    };

    const outDir = path.join(baseDir, 'elara');
    await mkdir(outDir, { recursive: true });
    const kernelJsonPath = path.join(outDir, 'kernel.json');
    await writeFile(kernelJsonPath, JSON.stringify(kernelSpec, null, 2) + '\n');

    console.log(`Wrote ${kernelJsonPath}`);
    console.log(JSON.stringify(kernelSpec, null, 2));
    console.log(`\nInstall it for the current user with:\n  jupyter kernelspec install "${outDir}" --user --name elara`);
}

async function writeCarpoKernelSpec(baseDir) {
    const exePath = resolveExecutable('carpo');
    if (!existsSync(exePath)) {
        console.warn(`Skipping Python (Carpo) kernelspec -- executable not found at ${exePath} (build it first: npm run build:native).`);
        return;
    }

    const pythonHome = await discoverPythonHome();
    if (!pythonHome) {
        console.warn('Skipping Python (Carpo) kernelspec -- no Python found; set PYTHONHOME to its prefix.');
        return;
    }
    const argv = [exePath, '-f', '{connection_file}', '--python-home', pythonHome];

    const kernelSpec = {
        argv,
        display_name: 'Python (Carpo)',
        language: 'python',
        interrupt_mode: 'message',
        ...libDirEnv(pythonHome)
    };

    const outDir = path.join(baseDir, 'carpo');
    await mkdir(outDir, { recursive: true });
    const kernelJsonPath = path.join(outDir, 'kernel.json');
    await writeFile(kernelJsonPath, JSON.stringify(kernelSpec, null, 2) + '\n');

    console.log(`Wrote ${kernelJsonPath}`);
    console.log(JSON.stringify(kernelSpec, null, 2));
    console.log(`\nInstall it for the current user with:\n  jupyter kernelspec install "${outDir}" --user --name carpo`);
}

async function discoverStataHome() {
    return (await runtimes()).discoverStataHome();
}

async function writeCallistoKernelSpec(baseDir) {
    const exePath = resolveExecutable('callisto');
    if (!existsSync(exePath)) {
        console.warn(`Skipping Stata (Callisto) kernelspec -- executable not found at ${exePath} (build it first: npm run build:native).`);
        return;
    }

    const stataHome = await discoverStataHome();
    if (!stataHome) {
        console.warn('Skipping Stata (Callisto) kernelspec -- no Stata 17+ found; set STATA_HOME to its directory.');
        return;
    }
    const argv = [exePath, '-f', '{connection_file}', '--stata-home', stataHome];

    const kernelSpec = {
        argv,
        display_name: 'Stata (Callisto)',
        language: 'stata',
        interrupt_mode: 'message'
    };

    const outDir = path.join(baseDir, 'callisto');
    await mkdir(outDir, { recursive: true });
    const kernelJsonPath = path.join(outDir, 'kernel.json');
    await writeFile(kernelJsonPath, JSON.stringify(kernelSpec, null, 2) + '\n');

    console.log(`Wrote ${kernelJsonPath}`);
    console.log(JSON.stringify(kernelSpec, null, 2));
    console.log(`\nInstall it for the current user with:\n  jupyter kernelspec install "${outDir}" --user --name callisto`);
}

async function main() {
    const positional = process.argv.slice(2).filter((arg) => !arg.startsWith('--'));
    const onlyArg = process.argv.find((arg) => arg.startsWith('--only='));
    const only = onlyArg ? onlyArg.slice('--only='.length) : 'all';
    const wants = (kernel) => only === 'all' || only === 'both' || only === kernel;

    const baseDir = positional[0] || path.join(REPO_ROOT, 'kernelspec');

    if (wants('r')) {
        await writeElaraKernelSpec(baseDir);
    }
    if (wants('python')) {
        await writeCarpoKernelSpec(baseDir);
    }
    if (wants('stata')) {
        await writeCallistoKernelSpec(baseDir);
    }
}

main().catch((err) => {
    console.error(err.message);
    process.exit(1);
});
