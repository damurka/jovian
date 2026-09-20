#!/usr/bin/env node
// Writes Jupyter kernelspecs (kernel.json) for elara.exe's and carpo.exe's
// -f/--connection-file launch mode (native/src/elara/elara.cpp,
// native/src/carpo/carpo.cpp) -- the standard "a frontend picks ports,
// writes a connection file, launches this argv with {connection_file}
// substituted in" protocol, as opposed to the themisto-specific
// --registration-port/--key mode used by lib/session/supervisor-client.ts.
//
// Writes BOTH kernelspecs by default (elara/kernel.json and
// carpo/kernel.json under the output directory) -- pass --only=r or
// --only=python to write just one, e.g. on a machine that only built one
// of the two (JOVIAN_BUILD_CARPO defaults ON now, but a stale build
// directory from before that change might still be missing carpo.exe). A
// missing executable for the *other* kernel only skips that one kernel's
// spec (with a warning), it doesn't abort the whole run.
//
// R_HOME/R_PATH/PYTHONHOME are baked into argv at generation time (same
// process.env.R_HOME/PYTHONHOME fallback pattern as tools/playground/
// server.js and test/integration/session-manager.test.ts) rather than
// looked up at kernel-launch time, since kernel.json's argv is static --
// re-run this after moving R/Python installs or rebuilding elara.exe/
// carpo.exe somewhere new.
import { mkdir, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { execSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, '../..');

// R_HOME isn't set as an inherited env var by every R install (confirmed
// via a real CI failure once this fell through to a hardcoded Windows-only
// path on Linux/macOS). `R RHOME` is R's own portable way of answering
// this on every platform, matching cmake/FindR.cmake's own technique; the
// hardcoded path remains only as a last resort for a Windows machine with
// R installed but not on PATH at all.
function discoverRHome() {
    if (process.env.R_HOME) return process.env.R_HOME;
    try {
        return execSync('R RHOME', { encoding: 'utf8' }).trim();
    } catch {
        return 'C:/Program Files/R/R-4.6.0';
    }
}

function defaultREnv() {
    const rHome = discoverRHome();
    return {
        rHome,
        rPath: process.env.R_PATH || `${rHome}/bin/x64`,
        rLibs: process.env.R_LIBS || ''
    };
}

// Same "ask the runtime itself" pattern as discoverRHome() above (and
// native/test/CMakeLists.txt's CARPO_TEST_PYTHON_HOME, which asks a
// CMake-discovered Python the same question) -- Python's own sys.prefix
// is the portable, correct answer to "what should PYTHONHOME be for this
// exact interpreter" on every platform. Tries `python3` before `python`
// since that's the more specific/unambiguous name where both exist.
function discoverPythonHome() {
    if (process.env.PYTHONHOME) return process.env.PYTHONHOME;
    for (const cmd of ['python3', 'python']) {
        try {
            return execSync(`${cmd} -c "import sys; print(sys.prefix)"`, { encoding: 'utf8' }).trim();
        } catch {
            // Try the next candidate command name.
        }
    }
    return process.platform === 'win32' ? 'C:/Python312' : '/usr';
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

    const { rHome, rPath, rLibs } = defaultREnv();
    const argv = [exePath, '-f', '{connection_file}', '--r-home', rHome, '--r-path', rPath];
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

    const pythonHome = discoverPythonHome();
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

async function main() {
    const positional = process.argv.slice(2).filter((arg) => !arg.startsWith('--'));
    const onlyArg = process.argv.find((arg) => arg.startsWith('--only='));
    const only = onlyArg ? onlyArg.slice('--only='.length) : 'both';

    const baseDir = positional[0] || path.join(REPO_ROOT, 'kernelspec');

    if (only !== 'python') {
        await writeElaraKernelSpec(baseDir);
    }
    if (only !== 'r') {
        await writeCarpoKernelSpec(baseDir);
    }
}

main().catch((err) => {
    console.error(err.message);
    process.exit(1);
});
