import { execFile } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, realpathSync } from 'node:fs';
import { posix, win32 } from 'node:path';
import type { EngineOptions, KernelType } from '../types/index.js';

type Maybe<T> = T | Promise<T>;

/** One installation of R, Python or Stata found on this machine. */
export interface RuntimeInstallation {
    /** What to pass as rHome / pythonHome / stataHome. */
    home: string;
    /** "4.6.0", "3.12.10", "19" -- when it could be told. */
    version?: string;
    /** A name for people: "R 4.6.0", "Python 3.12", "StataNow 19". */
    label: string;
    /** Stata only: the editions whose library is installed there, in the order they are tried. */
    editions?: Array<'mp' | 'se' | 'be'>;
    /** Stata only: whether a stata.lic is there (Stata will not start without one). */
    licensed?: boolean;
}

/** Runs a command and returns its last non-empty stdout line, or undefined if it failed or printed nothing. */
export type Runner = (command: string, args: string[]) => Maybe<string | undefined>;

export interface DiscoveryContext {
    run: Runner;
    env: Record<string, string | undefined>;
    platform: string;
    /** Like run, but every non-empty stdout line ([] if it failed). Default: runs the command. */
    runLines?: (command: string, args: string[]) => Maybe<string[]>;
    /** Names in a directory ([] if it cannot be read). Default: the real file system. */
    listDir?: (dir: string) => string[];
    /** Default: the real file system. */
    exists?: (path: string) => boolean;
    /** A text file's contents (undefined if it cannot be read). Default: the real file system. */
    readFile?: (path: string) => string | undefined;
    /** The canonical form of a path (long names, resolved links), for telling two spellings apart. Default: the real file system. */
    realpath?: (path: string) => string;
}

const listDirectory = (dir: string): string[] => {
    try {
        return readdirSync(dir);
    } catch {
        return [];
    }
};

const readTextFile = (path: string): string | undefined => {
    try {
        return readFileSync(path, 'utf8');
    } catch {
        return undefined;
    }
};

const canonicalPath = (path: string): string => {
    try {
        return realpathSync.native(path);
    } catch {
        return path;
    }
};

// Asynchronous, so looking for an installation never blocks the event loop
// (an Electron main process would otherwise freeze while `R RHOME` starts).
const runCommandLines = (command: string, args: string[]): Promise<string[]> =>
    new Promise((resolve) => {
        execFile(command, args, { encoding: 'utf8', timeout: 10_000, windowsHide: true }, (error, stdout) => {
            resolve(error ? [] : stdout.split(/\r?\n/).map((line) => line.trim()).filter(Boolean));
        });
    });

const runCommand: Runner = async (command, args) => (await runCommandLines(command, args)).at(-1);

const defaultContext = (): DiscoveryContext => ({ run: runCommand, env: process.env, platform: process.platform });

function helpers(context: DiscoveryContext) {
    const path = context.platform === 'win32' ? win32 : posix;
    return {
        path,
        listDir: context.listDir ?? listDirectory,
        exists: context.exists ?? existsSync,
        runLines: context.runLines ?? runCommandLines,
        readFile: context.readFile ?? readTextFile,
        // The same directory reached two ways (C:\PROGRA~1\R\R-46~1.0 from
        // `R RHOME`, C:\Program Files\R\R-4.6.0 from the folder scan) is one
        // installation.
        sameKey: (dir: string) => {
            const real = (context.realpath ?? canonicalPath)(dir);
            const normal = path.normalize(real).replace(/[\\/]+$/, '');
            return context.platform === 'win32' ? normal.toLowerCase() : normal;
        }
    };
}

/** `items` without the ones whose key() was already seen, first occurrence kept. */
function unique<T>(items: T[], key: (item: T) => string): T[] {
    const seen = new Set<string>();
    return items.filter((item) => {
        const k = key(item);
        if (seen.has(k)) return false;
        seen.add(k);
        return true;
    });
}

/** Compares dotted version strings ("4.10.1" > "4.9.3"); unparsable parts sort first. */
export function compareVersions(a: string, b: string): number {
    const pa = a.split(/[.\-_]/).map((p) => parseInt(p, 10));
    const pb = b.split(/[.\-_]/).map((p) => parseInt(p, 10));
    for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
        const x = Number.isNaN(pa[i]) || pa[i] === undefined ? -1 : pa[i]!;
        const y = Number.isNaN(pb[i]) || pb[i] === undefined ? -1 : pb[i]!;
        if (x !== y) return x - y;
    }
    return 0;
}

/** The entries of `parent` matching `pattern`, as full paths, newest version first. */
function newestSubdirectories(context: DiscoveryContext, parent: string, pattern: RegExp): string[] {
    const { path, listDir } = helpers(context);
    return listDir(parent)
        .filter((name) => pattern.test(name))
        .sort((a, b) => compareVersions(b.replace(/^\D+/, ''), a.replace(/^\D+/, '')))
        .map((name) => path.join(parent, name));
}

/** Where R is usually installed on this platform, newest version first. */
function wellKnownRHomes(context: DiscoveryContext): string[] {
    const { path } = helpers(context);
    const { env } = context;
    if (context.platform === 'win32') {
        const roots = [env.ProgramW6432, env.ProgramFiles, env.LOCALAPPDATA && path.join(env.LOCALAPPDATA, 'Programs')];
        return [...new Set(roots.filter((r): r is string => Boolean(r)))]
            .flatMap((root) => newestSubdirectories(context, path.join(root, 'R'), /^R-\d/));
    }
    if (context.platform === 'darwin') {
        return [
            '/Library/Frameworks/R.framework/Resources',
            // Every version installed side by side (rig keeps them all): R.framework/Versions/<x.y>[-arm64]/Resources
            ...newestSubdirectories(context, '/Library/Frameworks/R.framework/Versions', /^\d/).map((version) => path.join(version, 'Resources')),
            '/opt/homebrew/lib/R',
            '/usr/local/lib/R',
            // Homebrew's versioned kegs: <prefix>/Cellar/r/<version>/lib/R
            ...['/opt/homebrew', '/usr/local'].flatMap((prefix) =>
                newestSubdirectories(context, path.join(prefix, 'Cellar', 'r'), /^\d/).map((keg) => path.join(keg, 'lib', 'R')))
        ];
    }
    return [
        // rig and Posit's builds: /opt/R/<version>/lib/R
        ...newestSubdirectories(context, '/opt/R', /^\d/).map((version) => path.join(version, 'lib', 'R')),
        '/usr/lib/R',
        '/usr/local/lib/R',
        '/usr/lib64/R'
    ];
}

/** R's own version of an R home (library/base/DESCRIPTION), if it can be read. */
function rVersion(context: DiscoveryContext, home: string): string | undefined {
    const { path, readFile } = helpers(context);
    return readFile(path.join(home, 'library', 'base', 'DESCRIPTION'))?.match(/^Version:\s*(\S+)/m)?.[1];
}

/**
 * Every R installation found, the one discoverRHome() picks first: $R_HOME,
 * what `R RHOME` prints (R on PATH), on Windows the install path R's
 * installer records in the registry, then the usual install locations for the
 * platform, newest first. A directory only counts if it has R's base package
 * in it, so a stale registry entry or a leftover directory is skipped; the
 * same installation reached two ways is listed once.
 */
export async function listRInstallations(context: DiscoveryContext = defaultContext()): Promise<RuntimeInstallation[]> {
    const { path, exists, sameKey } = helpers(context);
    const isRHome = (dir: string | undefined): dir is string => Boolean(dir) && exists(path.join(dir!, 'library', 'base'));

    const candidates: Array<string | undefined> = [context.env.R_HOME, await context.run('R', ['RHOME'])];
    if (context.platform === 'win32') {
        const { runLines } = helpers(context);
        for (const hive of ['HKLM', 'HKCU']) {
            const line = await context.run('reg', ['query', `${hive}\\SOFTWARE\\R-core\\R`, '/v', 'InstallPath']);
            candidates.push(line?.match(/InstallPath\s+REG_SZ\s+(.+)$/)?.[1]?.trim());
            // Each version R's installer recorded (R-core\R\<version>\InstallPath), not only the default
            for (const versionLine of await runLines('reg', ['query', `${hive}\\SOFTWARE\\R-core\\R`, '/s', '/v', 'InstallPath'])) {
                candidates.push(versionLine.match(/^InstallPath\s+REG_SZ\s+(.+)$/)?.[1]?.trim());
            }
        }
    }
    candidates.push(...wellKnownRHomes(context));

    return unique(candidates.filter(isRHome), sameKey).map((home) => {
        const version = rVersion(context, home);
        return { home, ...(version ? { version } : {}), label: version ? `R ${version}` : 'R' };
    });
}

/**
 * Where R lives, when the caller did not say: $R_HOME (taken as given), else
 * the first of listRInstallations() -- `R RHOME`, the registry, the usual
 * install locations.
 */
export async function discoverRHome(context: DiscoveryContext = defaultContext()): Promise<string | undefined> {
    if (context.env.R_HOME) return context.env.R_HOME;
    return (await listRInstallations(context))[0]?.home;
}

// sys.base_prefix, not sys.prefix: inside a virtual environment the latter is
// the venv, which has no libpython to load. The version comes along on the
// same line, so each Python is started once.
const PYTHON_SCRIPT = 'import sys; print(sys.base_prefix + "|" + ".".join(map(str, sys.version_info[:3])))';

/**
 * Every Python installation found, the one discoverPythonHome() picks first:
 * $PYTHONHOME, the base prefix of `python3` / `python` on PATH (and of the
 * `py` launcher's default on Windows), then on Windows every other Python the
 * launcher knows (`py -0p`). Only prefixes that exist count; each is listed
 * once.
 */
export async function listPythonInstallations(context: DiscoveryContext = defaultContext()): Promise<RuntimeInstallation[]> {
    const { path, exists, runLines, sameKey } = helpers(context);
    const found: Array<{ home: string; version?: string }> = [];
    if (context.env.PYTHONHOME) found.push({ home: context.env.PYTHONHOME });

    const commands: Array<[string, string[]]> = [['python3', []], ['python', []]];
    if (context.platform === 'win32') commands.push(['py', ['-3']]);
    for (const [command, args] of commands) {
        const answer = await context.run(command, [...args, '-c', PYTHON_SCRIPT]);
        const [home, version] = (answer ?? '').split('|');
        if (home) found.push({ home, ...(version ? { version } : {}) });
    }

    // Pythons outside PATH: pyenv's, conda's (base and environments), and on macOS python.org's frameworks
    for (const executable of otherPythonExecutables(context)) {
        if (!exists(executable)) continue;
        const answer = await context.run(executable, ['-c', PYTHON_SCRIPT]);
        const [home, version] = (answer ?? '').split('|');
        if (home) found.push({ home, ...(version ? { version } : {}) });
    }

    if (context.platform === 'win32') {
        // " -V:3.12[-64] *   C:\...\python.exe" (older launchers: " -3.12-64 *  ...")
        for (const line of await runLines('py', ['-0p'])) {
            const match = line.trim().match(/^-(?:V:)?([\d.]+)\S*\s+(?:\*\s+)?(.+\.exe)$/i);
            if (match) found.push({ home: path.dirname(match[2]!.trim()), version: match[1]! });
        }
    }

    return unique(found.filter(({ home }) => exists(home)), ({ home }) => sameKey(home)).map(({ home, version }) => ({
        home,
        ...(version ? { version } : {}),
        label: version ? `Python ${version}` : 'Python'
    }));
}

/** The interpreters of Pythons that are often not on PATH: pyenv's versions, conda's base and environments, python.org's macOS frameworks. */
function otherPythonExecutables(context: DiscoveryContext): string[] {
    const { path, listDir } = helpers(context);
    const windows = context.platform === 'win32';
    const home = context.env.USERPROFILE ?? context.env.HOME;
    const interpreter = (prefix: string) => windows ? path.join(prefix, 'python.exe') : path.join(prefix, 'bin', 'python3');
    const childrenOf = (dir: string) => listDir(dir).map((name) => path.join(dir, name));
    const prefixes: string[] = [];
    if (home) {
        const pyenv = context.env.PYENV_ROOT ?? (windows ? path.join(home, '.pyenv', 'pyenv-win') : path.join(home, '.pyenv'));
        prefixes.push(...childrenOf(path.join(pyenv, 'versions')));
        for (const conda of ['miniconda3', 'anaconda3', 'miniforge3', 'mambaforge']) {
            const base = path.join(home, conda);
            prefixes.push(base, ...childrenOf(path.join(base, 'envs')));
        }
    }
    if (context.env.CONDA_PREFIX) prefixes.push(context.env.CONDA_PREFIX);
    if (context.platform === 'darwin') {
        prefixes.push(...newestSubdirectories(context, '/Library/Frameworks/Python.framework/Versions', /^\d/));
    }
    return prefixes.map(interpreter);
}

/**
 * Which Python to embed, when the caller did not say: $PYTHONHOME (taken as
 * given), else the first of listPythonInstallations() -- the Python on PATH.
 */
export async function discoverPythonHome(context: DiscoveryContext = defaultContext()): Promise<string | undefined> {
    if (context.env.PYTHONHOME) return context.env.PYTHONHOME;
    return (await listPythonInstallations(context))[0]?.home;
}

/**
 * Where each edition's shared library sits in a Stata directory (what
 * callisto loads -- see native/src/callisto/stata/stata_dynlib.cpp), in the
 * order callisto tries them.
 */
function stataLibraries(platform: string): Array<{ edition: 'mp' | 'se' | 'be'; file: string }> {
    if (platform === 'win32') {
        return (['mp', 'se', 'be'] as const).map((edition) => ({ edition, file: `${edition}-64.dll` }));
    }
    if (platform === 'darwin') {
        return (['mp', 'se', 'be'] as const).map((edition) => ({
            edition,
            file: `Stata${edition.toUpperCase()}.app/Contents/MacOS/libstata-${edition}.dylib`
        }));
    }
    return [
        { edition: 'mp', file: 'libstata-mp.so' },
        { edition: 'se', file: 'libstata-se.so' },
        { edition: 'be', file: 'libstata.so' }
    ];
}

const STATA_NAME = /^stata\s*(now)?\s*\d*$/i;

/**
 * The directories Stata's Windows installer recorded (its entries under the
 * Uninstall key: DisplayName "Stata18" / "StataNow19", InstallLocation), so a
 * Stata installed outside Program Files is found too. Each comes with its
 * DisplayName, which carries the version.
 */
async function stataRegistryHomes(context: DiscoveryContext): Promise<Array<{ dir: string; name: string }>> {
    const { runLines } = helpers(context);
    const homes: Array<{ dir: string; name: string }> = [];
    for (const hive of ['HKLM', 'HKCU']) {
        const root = `${hive}\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall`;
        let key: string | undefined;
        for (const line of await runLines('reg', ['query', root, '/s', '/f', 'Stata', '/d'])) {
            if (/^HKEY_/i.test(line)) {
                key = line;
                continue;
            }
            const name = line.match(/^DisplayName\s+REG_SZ\s+(.+)$/)?.[1]?.trim();
            if (!key || !name || !STATA_NAME.test(name)) continue;
            const location = (await context.run('reg', ['query', key, '/v', 'InstallLocation']))
                ?.match(/InstallLocation\s+REG_SZ\s+(.+)$/)?.[1]?.trim();
            if (location) homes.push({ dir: location.replace(/[\\/]+$/, ''), name });
        }
    }
    return homes;
}

/**
 * Every Stata 17+ installation found, best first:
 *   - the standard install locations: `Stata19`, `StataNow19`, ... under
 *     Program Files (Windows), `Stata*` under /Applications (macOS),
 *     `stata*` under /usr/local and /opt (Linux);
 *   - on Windows, where Stata's installer says it installed Stata;
 *   - directories on PATH (a Linux install is often put there).
 * A directory only counts if it has Stata's shared library in it (Stata 16
 * and older do not). Each comes with the editions installed there and
 * whether it is licensed. Best means: licensed (a stata.lic next to it --
 * Stata will not start without one) before unlicensed, then the newest
 * version, and StataNow before Stata of the same version.
 */
export async function listStataInstallations(context: DiscoveryContext = defaultContext()): Promise<RuntimeInstallation[]> {
    const windows = context.platform === 'win32';
    const { path, listDir, exists, sameKey } = helpers(context);

    const roots = windows
        ? [context.env.ProgramW6432, context.env.ProgramFiles, 'C:\\Program Files']
        : context.platform === 'darwin' ? ['/Applications'] : ['/usr/local', '/opt'];

    const found: Array<{ dir: string; name: string }> = [];
    if (context.env.STATA_HOME) found.push({ dir: context.env.STATA_HOME, name: path.basename(context.env.STATA_HOME) });
    for (const root of new Set(roots.filter((r): r is string => Boolean(r)))) {
        for (const name of listDir(root)) {
            if (STATA_NAME.test(name)) found.push({ dir: path.join(root, name), name });
        }
    }
    if (windows) found.push(...await stataRegistryHomes(context));
    for (const dir of (context.env.PATH ?? '').split(path.delimiter).filter(Boolean)) {
        found.push({ dir: dir.replace(/[\\/]+$/, ''), name: path.basename(dir) });
    }

    const libraries = stataLibraries(context.platform);
    const candidates = unique(found, ({ dir }) => sameKey(dir))
        .map(({ dir, name }) => {
            const version = Number(name.match(/(\d+)\D*$/)?.[1] ?? 0);
            const now = /^stata\s*now/i.test(name);
            return {
                home: dir,
                ...(version ? { version: String(version) } : {}),
                label: `${now ? 'StataNow' : 'Stata'}${version ? ` ${version}` : ''}`,
                editions: libraries.filter(({ file }) => exists(path.join(dir, file))).map(({ edition }) => edition),
                licensed: exists(path.join(dir, 'stata.lic')),
                rank: { version, now }
            };
        })
        .filter(({ editions }) => editions.length > 0);

    candidates.sort((a, b) =>
        Number(b.licensed) - Number(a.licensed) || b.rank.version - a.rank.version || Number(b.rank.now) - Number(a.rank.now));
    return candidates.map(({ rank: _rank, ...installation }) => installation);
}

/**
 * Which Stata to embed, when the caller did not say: $STATA_HOME (taken as
 * given), else the first of listStataInstallations() -- licensed first, then
 * the newest.
 */
export async function discoverStataHome(context: DiscoveryContext = defaultContext()): Promise<string | undefined> {
    if (context.env.STATA_HOME) return context.env.STATA_HOME;
    return (await listStataInstallations(context))[0]?.home;
}

/**
 * The ark executable (Posit's R kernel), when the caller did not say:
 * $ARK_PATH, else the one bundled with Positron (for this platform and CPU),
 * else ark on PATH.
 */
export async function discoverArkPath(context: DiscoveryContext = defaultContext()): Promise<string | undefined> {
    if (context.env.ARK_PATH) return context.env.ARK_PATH;
    const { path, exists, listDir } = helpers(context);
    const windows = context.platform === 'win32';
    const exe = windows ? 'ark.exe' : 'ark';

    const positronApps = windows
        ? [
            context.env.LOCALAPPDATA && path.join(context.env.LOCALAPPDATA, 'Programs', 'Positron'),
            context.env.ProgramW6432 && path.join(context.env.ProgramW6432, 'Positron'),
            context.env.ProgramFiles && path.join(context.env.ProgramFiles, 'Positron')
        ]
        : context.platform === 'darwin'
            ? ['/Applications/Positron.app/Contents/Resources']
            : ['/usr/share/positron', '/usr/lib/positron', '/opt/positron'];

    const arch = context.env.PROCESSOR_ARCHITECTURE?.toLowerCase() === 'arm64' || process.arch === 'arm64' ? 'arm64' : 'x64';
    for (const app of positronApps.filter((a): a is string => Boolean(a))) {
        const arkDir = path.join(app, 'resources', 'app', 'extensions', 'positron-r', 'resources', 'ark');
        // Newer Positron: one subfolder per platform (windows-x64, darwin-universal, linux-arm64 ...).
        const inSubfolders = listDir(arkDir)
            .sort((a, b) => Number(b.endsWith(arch)) - Number(a.endsWith(arch)))
            .map((sub) => path.join(arkDir, sub, exe));
        const found = [path.join(arkDir, exe), ...inSubfolders].find(exists);
        if (found) return found;
    }
    for (const dir of (context.env.PATH ?? '').split(path.delimiter).filter(Boolean)) {
        const candidate = path.join(dir, exe);
        if (exists(candidate)) return candidate;
    }
    return undefined;
}

// What each kernel type needs found when the caller did not give it.
type DiscoveredField = 'rHome' | 'pythonHome' | 'stataHome' | 'arkPath';
const NEEDS: Record<KernelType, DiscoveredField[]> = {
    r: ['rHome'],
    python: ['pythonHome'],
    stata: ['stataHome'],
    ark: ['rHome', 'arkPath']
};
const DISCOVER: Record<DiscoveredField, (context?: DiscoveryContext) => Promise<string | undefined>> = {
    rHome: discoverRHome,
    pythonHome: discoverPythonHome,
    stataHome: discoverStataHome,
    arkPath: discoverArkPath
};

// What the real machine answered, per option, for the life of the process.
// Only a success is kept: after a failed search the next session looks again
// (R may have been installed in the meantime).
const found = new Map<DiscoveredField, Promise<string | undefined>>();

function discoverOnce(field: DiscoveredField): Promise<string | undefined> {
    let search = found.get(field);
    if (!search) {
        search = DISCOVER[field]();
        found.set(field, search);
        search.then((value) => { if (!value) found.delete(field); }, () => found.delete(field));
    }
    return search;
}

/** Forgets what discovery found on this machine, so the next session searches again. */
export function forgetDiscoveredRuntimes(): void {
    found.clear();
}

/**
 * The options with rHome (R sessions), pythonHome (Python sessions),
 * stataHome (Stata sessions) or rHome + arkPath (Ark sessions) filled in when the caller left them out and the
 * runtime could be found. Anything the caller passed is kept as it is; if
 * nothing is found the field stays unset, and the kernel reports what it
 * could not find. Without a context, the machine is searched once per
 * process and the answer reused.
 */
export async function withDiscoveredRuntime(options: EngineOptions, context?: DiscoveryContext): Promise<EngineOptions> {
    const missing = (NEEDS[options.kernelType ?? 'r'] ?? []).filter((field) => !options[field]);
    if (missing.length === 0) return options;
    let result = options;
    for (const field of missing) {
        const value = context ? await DISCOVER[field](context) : await discoverOnce(field);
        if (value) result = { ...result, [field]: value };
    }
    return result;
}

/**
 * The options with every relative path made absolute against `cwd` (the
 * caller's working directory). The kernel process starts in workingDirectory, so a
 * relative rHome / pythonHome / stataHome / ... would otherwise be resolved
 * from there -- or, for workingDirectory itself, from wherever the supervisor
 * happened to start. rLibs and pythonPath are lists (separated like PATH);
 * each entry is resolved.
 */
export function withAbsolutePaths(options: EngineOptions, cwd: string = process.cwd(), platform: string = process.platform): EngineOptions {
    const path = platform === 'win32' ? win32 : posix;
    const single = ['rHome', 'rPath', 'pandocPath', 'pythonHome', 'venvPath', 'stataHome', 'arkPath', 'workingDirectory'] as const;
    const lists = ['rLibs', 'pythonPath'] as const;

    // An absolute path is passed on exactly as written.
    const absolute = (value: string) => (!value || path.isAbsolute(value) ? value : path.resolve(cwd, value));

    const result: EngineOptions = { ...options };
    for (const key of single) {
        const value = options[key];
        if (value) result[key] = absolute(value);
    }
    for (const key of lists) {
        const value = options[key];
        if (value) result[key] = value.split(path.delimiter).map(absolute).join(path.delimiter);
    }
    return result;
}
