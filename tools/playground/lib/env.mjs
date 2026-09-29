// Finds the R, Python and Stata installations a session should use, to
// pre-fill the new-session dialog. Used server-side by the Next.js app.
//
// The searching itself is the library's (lib/session/runtimes.ts:
// discoverRHome / discoverPythonHome / discoverStataHome), loaded from the
// built library, so the playground finds exactly what SessionManager would --
// not a second copy of that logic that could drift from it.
import { existsSync } from 'node:fs';
import { homedir } from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const isWindows = process.platform === 'win32';

// These probe machine-specific locations discovered at runtime, which is the
// whole point of this module -- not project files. The bundler's static
// tracer can't know that and would otherwise pull the whole project into the
// server output, so the dynamic calls are funnelled through these
// wrappers, which carry its opt-out marker.
const exists = (p) => existsSync(/* turbopackIgnore: true */ p);
const join = (...parts) => path.join(/* turbopackIgnore: true */ ...parts);

// The built library under <repo>/dist/lib, loaded at run time like
// server/registry.ts loads SessionManager (and for the same bundler reasons).
let runtimesModule;
async function runtimes() {
    const distDir = process.env.JOVIAN_DIST_DIR
        ?? path.resolve(/* turbopackIgnore: true */ process.cwd(), '..', '..', 'dist', 'lib');
    runtimesModule ??= import(/* webpackIgnore: true */ /* turbopackIgnore: true */ pathToFileURL(join(distDir, 'session', 'runtimes.js')).href);
    return runtimesModule;
}

// [] when nothing is found, or the library is not built yet.
async function list(name) {
    try {
        return (await (await runtimes())[name]()) ?? [];
    } catch {
        return [];
    }
}

/**
 * Directory holding R's shared library that has to be on the loader path.
 * Only Windows needs it spelled out (R.dll lives in bin/x64); the supervisor
 * derives the same directory from R_HOME when this is left empty, so it is
 * only shown to be edited.
 */
export function discoverRPath(rHome) {
    if (process.env.R_PATH) return process.env.R_PATH;
    return rDllDirectory(rHome);
}

// The folder with R.dll in one R installation ('' off Windows).
function rDllDirectory(rHome) {
    if (!isWindows || !rHome) return '';
    for (const sub of [join('bin', 'x64'), 'bin']) {
        const dir = join(rHome, sub);
        if (exists(join(dir, 'R.dll'))) return dir;
    }
    return '';
}

/**
 * Everything the UI needs to pre-fill the new-session dialog: every
 * installation found, and the one a session uses when none is chosen -- the
 * environment variable when set, else the first found, which is exactly
 * discoverRHome() / discoverPythonHome() / discoverStataHome()'s rule.
 */
export async function defaultEnvironment() {
    const [r, python, stata] = await Promise.all([
        list('listRInstallations'),
        list('listPythonInstallations'),
        list('listStataInstallations')
    ]);
    const rHome = process.env.R_HOME || r[0]?.home || '';
    return {
        rHome,
        rPath: discoverRPath(rHome),
        rLibs: process.env.R_LIBS || '',
        pythonHome: process.env.PYTHONHOME || python[0]?.home || '',
        pythonPath: process.env.PYTHONPATH || '',
        venvPath: process.env.VIRTUAL_ENV || '',
        stataHome: process.env.STATA_HOME || stata[0]?.home || '',
        installations: {
            r: r.map((installation) => ({ ...installation, rPath: rDllDirectory(installation.home) })),
            python,
            stata
        },
        platform: process.platform,
        homeDirectory: homedir()
    };
}
