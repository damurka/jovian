// Finds the R and Python installations a session should use, on Windows,
// macOS and Linux. Used server-side by the Next.js app; no path in it assumes
// a platform, a drive letter or one developer's machine.
//
// Order for R: the R_HOME env var -> `R RHOME` (R's own portable way of
// answering, the same technique cmake/FindR.cmake uses) -> the Windows
// registry -> well-known install locations for the platform (newest
// version wins). Order for Python: PYTHONHOME -> python3 / python (and the
// `py` launcher on Windows) asked for sys.base_prefix.
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import path from 'node:path';

const isWindows = process.platform === 'win32';

// These probe machine-specific locations discovered at runtime, which is the
// whole point of this module -- not project files. The bundler's static
// tracer can't know that and would otherwise pull the whole project into the
// server output, so the dynamic calls are funnelled through these two
// wrappers, which carry its opt-out marker.
const exists = (p) => existsSync(/* turbopackIgnore: true */ p);
const join = (...parts) => path.join(/* turbopackIgnore: true */ ...parts);
const listDirectory = (dir) => readdirSync(/* turbopackIgnore: true */ dir, { withFileTypes: true });

function run(command, args, timeout = 8000) {
    try {
        return execFileSync(command, args, {
            encoding: 'utf8',
            timeout,
            windowsHide: true,
            stdio: ['ignore', 'pipe', 'ignore']
        }).trim();
    } catch {
        return '';
    }
}

/** Compares dotted version strings ("4.10.1" > "4.9.3"); unparsable parts sort last. */
export function compareVersions(a, b) {
    const pa = String(a).split(/[.\-_]/).map((p) => parseInt(p, 10));
    const pb = String(b).split(/[.\-_]/).map((p) => parseInt(p, 10));
    for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
        const x = Number.isNaN(pa[i]) || pa[i] === undefined ? -1 : pa[i];
        const y = Number.isNaN(pb[i]) || pb[i] === undefined ? -1 : pb[i];
        if (x !== y) return x - y;
    }
    return 0;
}

// A real R home has its base library; checking for it filters out a stray
// directory that merely has a matching name.
export function looksLikeRHome(dir) {
    return Boolean(dir) && exists(join(dir, 'library', 'base'));
}

function newestSubdirectory(parent, pattern) {
    let entries;
    try {
        entries = listDirectory(parent);
    } catch {
        return [];
    }
    return entries
        .filter((e) => e.isDirectory() && pattern.test(e.name))
        .map((e) => e.name)
        .sort((a, b) => compareVersions(b.replace(/^\D+/, ''), a.replace(/^\D+/, '')))
        .map((name) => join(parent, name));
}

function wellKnownRHomes() {
    const candidates = [];
    if (isWindows) {
        const roots = [process.env.ProgramFiles, process.env.ProgramW6432, process.env.LOCALAPPDATA && join(process.env.LOCALAPPDATA, 'Programs')]
            .filter(Boolean);
        for (const root of new Set(roots)) {
            candidates.push(...newestSubdirectory(join(root, 'R'), /^R-\d/));
        }
    } else if (process.platform === 'darwin') {
        candidates.push(
            '/Library/Frameworks/R.framework/Resources',
            '/opt/homebrew/lib/R',
            '/usr/local/lib/R'
        );
        // Homebrew keeps versioned kegs: <prefix>/Cellar/r/<version>/lib/R
        for (const prefix of ['/opt/homebrew', '/usr/local']) {
            for (const keg of newestSubdirectory(join(prefix, 'Cellar', 'r'), /^\d/)) {
                candidates.push(join(keg, 'lib', 'R'));
            }
        }
    } else {
        // rig / r-project's CRAN builds install to /opt/R/<version>/lib/R.
        for (const version of newestSubdirectory('/opt/R', /^\d/)) {
            candidates.push(join(version, 'lib', 'R'));
        }
        candidates.push('/usr/lib/R', '/usr/local/lib/R', '/usr/lib64/R');
    }
    return candidates;
}

/** Returns the R_HOME to use, or '' when none could be found. */
export function discoverRHome() {
    if (process.env.R_HOME && looksLikeRHome(process.env.R_HOME)) {
        return process.env.R_HOME;
    }

    const fromR = run('R', ['RHOME']);
    if (fromR && looksLikeRHome(fromR)) {
        return fromR;
    }

    if (isWindows) {
        // HKLM\SOFTWARE\R-core\R\InstallPath is what R's installer writes.
        const out = run('reg', ['query', 'HKLM\\SOFTWARE\\R-core\\R', '/v', 'InstallPath']);
        const match = out.match(/InstallPath\s+REG_SZ\s+(.+)$/m);
        if (match && looksLikeRHome(match[1].trim())) {
            return match[1].trim();
        }
    }

    return wellKnownRHomes().find(looksLikeRHome) ?? '';
}

/**
 * Directory holding R's shared library that has to be on the loader path.
 * Only Windows needs it spelled out (R.dll lives in bin/x64 and Windows
 * resolves an executable's DLL imports before any of our code runs); on
 * Linux/macOS the supervisor derives what it needs from R_HOME itself.
 */
export function discoverRPath(rHome) {
    if (process.env.R_PATH) return process.env.R_PATH;
    if (!isWindows || !rHome) return '';
    for (const sub of [join('bin', 'x64'), 'bin']) {
        const dir = join(rHome, sub);
        if (exists(join(dir, 'R.dll'))) return dir;
    }
    return '';
}

export function defaultREnv() {
    const rHome = discoverRHome();
    return {
        rHome,
        rPath: discoverRPath(rHome),
        rLibs: process.env.R_LIBS || ''
    };
}

/** [command, ...args] candidates for launching a Python, most specific first. */
function pythonCommands() {
    const commands = [['python3'], ['python']];
    if (isWindows) commands.push(['py', '-3']);
    return commands;
}

/**
 * The Python "home" (prefix holding the standard library and the shared
 * library carpo loads). sys.base_prefix, not sys.prefix: inside a virtual
 * environment sys.prefix is the venv, which has neither.
 */
export function discoverPythonHome() {
    if (process.env.PYTHONHOME && exists(process.env.PYTHONHOME)) {
        return process.env.PYTHONHOME;
    }
    for (const [command, ...args] of pythonCommands()) {
        const out = run(command, [...args, '-c', 'import sys; print(getattr(sys, "base_prefix", sys.prefix))']);
        if (out && exists(out)) return out;
    }
    return '';
}

export function defaultPythonEnv() {
    return {
        pythonHome: discoverPythonHome(),
        pythonPath: process.env.PYTHONPATH || '',
        venvPath: process.env.VIRTUAL_ENV || ''
    };
}

/** Everything the UI needs to pre-fill the new-session dialog. */
export function defaultEnvironment() {
    return {
        ...defaultREnv(),
        ...defaultPythonEnv(),
        platform: process.platform,
        homeDirectory: homedir()
    };
}
