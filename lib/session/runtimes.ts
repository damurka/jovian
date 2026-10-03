import { execFile } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, realpathSync } from 'node:fs';
import { posix, win32 } from 'node:path';
import type { EngineOptions, KernelType } from '../types/index.js';

type Maybe<T> = T | Promise<T>;

/**
 * How an installation was found: the caller's choice (findRuntime()'s `home`), an environment variable (R_HOME,
 * PYTHONHOME, STATA_HOME), PATH, the Windows registry, the Windows `py` launcher, or a usual install folder.
 */
export type RuntimeSource = 'setting' | 'env' | 'path' | 'registry' | 'launcher' | 'folder';

/** One installation of R, Python or Stata found on this machine. */
export interface RuntimeInstallation {
    /** What to pass as rHome / pythonHome / stataHome. */
    home: string;
    /** "4.6.0", "3.12.10", "19" -- when it could be told. */
    version?: string;
    /** A name for people: "R 4.6.0", "Python 3.12", "StataNow 19". */
    label: string;
    /** How it was found. */
    source?: RuntimeSource;
    /**
     * Whether Jovian's kernel can run it. A Python without its shared library (libpython, python3XY.dll) cannot be
     * embedded by the Python kernel; Stata will not start without a licence.
     */
    usable: boolean;
    /** Why it is not usable, for people. */
    problem?: string;
    /** Python only: its interpreter. */
    executable?: string;
    /** Stata only: the editions whose library is installed there, in the order they are tried. */
    editions?: Array<'mp' | 'se' | 'be'>;
    /** Stata only: whether a stata.lic is there (Stata will not start without one). */
    licensed?: boolean;
}

/** An installation findRuntime() chose, and whether it is as new as asked. */
export interface FoundRuntime extends RuntimeInstallation {
    /** Whether its version is at least the `minVersion` asked for (true when none was, or the version is unknown). */
    meetsMinimum: boolean;
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

    const candidates: Array<{ dir: string | undefined; source: RuntimeSource }> = [
        { dir: context.env.R_HOME, source: 'env' },
        { dir: await context.run('R', ['RHOME']), source: 'path' }
    ];
    if (context.platform === 'win32') {
        const { runLines } = helpers(context);
        for (const hive of ['HKLM', 'HKCU']) {
            const line = await context.run('reg', ['query', `${hive}\\SOFTWARE\\R-core\\R`, '/v', 'InstallPath']);
            candidates.push({ dir: line?.match(/InstallPath\s+REG_SZ\s+(.+)$/)?.[1]?.trim(), source: 'registry' });
            // Each version R's installer recorded (R-core\R\<version>\InstallPath), not only the default
            for (const versionLine of await runLines('reg', ['query', `${hive}\\SOFTWARE\\R-core\\R`, '/s', '/v', 'InstallPath'])) {
                candidates.push({ dir: versionLine.match(/^InstallPath\s+REG_SZ\s+(.+)$/)?.[1]?.trim(), source: 'registry' });
            }
        }
    }
    candidates.push(...wellKnownRHomes(context).map((dir) => ({ dir, source: 'folder' as const })));

    return unique(candidates.filter((c): c is { dir: string; source: RuntimeSource } => isRHome(c.dir)), (c) => sameKey(c.dir))
        .map(({ dir, source }) => describeR(context, dir, source));
}

/** An R home as an installation (its version read from its base package). */
function describeR(context: DiscoveryContext, home: string, source: RuntimeSource): RuntimeInstallation {
    const version = rVersion(context, home);
    return { home, ...(version ? { version } : {}), label: version ? `R ${version}` : 'R', source, usable: true };
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

// What each Python is asked, one line: its base prefix (sys.base_prefix, not sys.prefix: inside a virtual
// environment the latter is the venv, which has no libpython to load), its version, its interpreter, and whether
// the prefix has Python's shared library -- what the Python kernel loads: python3XY.dll on Windows,
// libpython3.X.so / .dylib in lib/, lib64/ or lib/<arch>-linux-gnu/ elsewhere. Each Python is started once.
export const PYTHON_SCRIPT = [
    'import glob, os, sys',
    'v = sys.version_info',
    'p = sys.base_prefix',
    'n = "python%d%d.dll" % (v[0], v[1]) if os.name == "nt" else "libpython%d.%d*" % (v[0], v[1])',
    'd = [p] if os.name == "nt" else [os.path.join(p, "lib"), os.path.join(p, "lib64")] + glob.glob(os.path.join(p, "lib", "*-linux-gnu"))',
    'print("|".join([p, "%d.%d.%d" % (v[0], v[1], v[2]), sys.executable, "1" if any(glob.glob(os.path.join(x, n)) for x in d) else "0"]))'
].join('; ');

interface PythonAnswer {
    home: string;
    version?: string;
    executable?: string;
    /** Whether its shared library is there; undefined when the answer did not say. */
    shared?: boolean;
    source: RuntimeSource;
}

/** A Python's answer to PYTHON_SCRIPT ("home|version|executable|1"), or undefined when it gave none. */
function parsePythonAnswer(answer: string | undefined, source: RuntimeSource): PythonAnswer | undefined {
    const [home, version, executable, shared] = (answer ?? '').split('|');
    if (!home) return undefined;
    return {
        home,
        ...(version ? { version } : {}),
        ...(executable ? { executable } : {}),
        ...(shared === '1' ? { shared: true } : shared === '0' ? { shared: false } : {}),
        source
    };
}

/** A Python's answer as an installation: usable unless it said its shared library is missing. */
function describePython({ home, version, executable, shared, source }: PythonAnswer): RuntimeInstallation {
    return {
        home,
        ...(version ? { version } : {}),
        label: version ? `Python ${version}` : 'Python',
        source,
        usable: shared !== false,
        ...(shared === false ? { problem: 'it has no shared library (libpython / python3XY.dll), which the Python kernel loads; the python.org installers include it' } : {}),
        ...(executable ? { executable } : {})
    };
}

// Windows' "App execution alias" python.exe (WindowsApps) only offers to install Python from the Store
const isStoreAlias = (executable: string | undefined) => Boolean(executable && /[\\/]WindowsApps[\\/]/i.test(executable));

/**
 * Every Python installation found, the one discoverPythonHome() picks first:
 * $PYTHONHOME, on Windows the `py` launcher's default, the base prefix of
 * `python3` / `python` on PATH, Pythons outside PATH (pyenv, conda, macOS
 * frameworks), then on Windows every other Python the launcher knows
 * (`py -0p`). Only prefixes that exist count; each is listed once. A Python
 * without its shared library is listed, not usable.
 */
export async function listPythonInstallations(context: DiscoveryContext = defaultContext()): Promise<RuntimeInstallation[]> {
    const { exists, runLines, sameKey } = helpers(context);
    const found: PythonAnswer[] = [];
    if (context.env.PYTHONHOME) {
        // asked through its interpreter, for its version and shared library; listed as it is when it has none
        const executable = pythonExecutablesIn(context, context.env.PYTHONHOME).find((candidate) => exists(candidate));
        const answer = executable ? parsePythonAnswer(await context.run(executable, ['-c', PYTHON_SCRIPT]), 'env') : undefined;
        found.push(answer ?? { home: context.env.PYTHONHOME, source: 'env' });
    }

    // In this order: the launcher's default first on Windows (the Python the user chose there), python3 / python on
    // PATH, Pythons outside PATH (pyenv's, conda's base and environments, macOS python.org's frameworks), then on
    // Windows every Python the launcher knows (" -V:3.12[-64] *   C:\...\python.exe"; older launchers
    // " -3.12-64 *  ..."). Each is asked, for its shared library -- all at once, kept in this order.
    const ask = async (command: string, args: string[], source: RuntimeSource) =>
        parsePythonAnswer(await context.run(command, [...args, '-c', PYTHON_SCRIPT]), source);
    const onPath: Array<Promise<PythonAnswer | undefined>> = (context.platform === 'win32'
        ? [['py', ['-3'], 'launcher'], ['python3', [], 'path'], ['python', [], 'path']] as const
        : [['python3', [], 'path'], ['python', [], 'path']] as const).map(([command, args, source]) => ask(command, [...args], source));
    const elsewhere = otherPythonExecutables(context).filter((executable) => exists(executable)).map((executable) => ask(executable, [], 'folder'));
    const fromLauncher = context.platform === 'win32'
        ? Promise.resolve(runLines('py', ['-0p'])).then((lines) => Promise.all(lines
            .map((line) => line.trim().match(/^-(?:V:)?[\d.]+\S*\s+(?:\*\s+)?(.+\.exe)$/i)?.[1]?.trim())
            .filter((executable): executable is string => Boolean(executable))
            .map((executable) => ask(executable, [], 'launcher'))))
        : Promise.resolve([]);
    const [first, second, third] = await Promise.all([Promise.all(onPath), Promise.all(elsewhere), fromLauncher]);
    found.push(...[...first, ...second, ...third].filter((answer): answer is PythonAnswer => Boolean(answer)));

    return unique(found.filter(({ home, executable }) => exists(home) && !isStoreAlias(executable)), ({ home }) => sameKey(home)).map(describePython);
}

/** The interpreters a Python path may mean: the path itself when it is one, else those in the installation folder. */
function pythonExecutablesIn(context: DiscoveryContext, path: string): string[] {
    const { path: p } = helpers(context);
    const name = p.basename(path.replace(/[\\/]+$/, ''));
    if (context.platform === 'win32' ? /\.exe$/i.test(name) : /^python(\d+(\.\d+)?)?$/.test(name)) return [path];
    return context.platform === 'win32'
        ? [p.join(path, 'python.exe'), p.join(path, 'Scripts', 'python.exe')]
        : [p.join(path, 'bin', 'python3'), p.join(path, 'bin', 'python'), p.join(path, 'python3')];
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
 * given), else the first usable one of listPythonInstallations() (one with its
 * shared library), else the first.
 */
export async function discoverPythonHome(context: DiscoveryContext = defaultContext()): Promise<string | undefined> {
    if (context.env.PYTHONHOME) return context.env.PYTHONHOME;
    const pythons = await listPythonInstallations(context);
    return (pythons.find((python) => python.usable) ?? pythons[0])?.home;
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

    const found: Array<{ dir: string; name: string; source: RuntimeSource }> = [];
    if (context.env.STATA_HOME) found.push({ dir: context.env.STATA_HOME, name: path.basename(context.env.STATA_HOME), source: 'env' });
    for (const root of new Set(roots.filter((r): r is string => Boolean(r)))) {
        for (const name of listDir(root)) {
            if (STATA_NAME.test(name)) found.push({ dir: path.join(root, name), name, source: 'folder' });
        }
    }
    if (windows) found.push(...(await stataRegistryHomes(context)).map((home) => ({ ...home, source: 'registry' as const })));
    for (const dir of (context.env.PATH ?? '').split(path.delimiter).filter(Boolean)) {
        found.push({ dir: dir.replace(/[\\/]+$/, ''), name: path.basename(dir), source: 'path' });
    }

    const candidates = unique(found, ({ dir }) => sameKey(dir))
        .map(({ dir, name, source }) => ({ ...describeStata(context, dir, name, source), rank: stataRank(name) }))
        .filter(({ editions }) => editions!.length > 0);

    candidates.sort((a, b) =>
        Number(b.licensed) - Number(a.licensed) || b.rank.version - a.rank.version || Number(b.rank.now) - Number(a.rank.now));
    return candidates.map(({ rank: _rank, ...installation }) => installation);
}

/** A Stata directory's version and kind from its name ("StataNow19" -> 19, now). */
function stataRank(name: string): { version: number; now: boolean } {
    return { version: Number(name.match(/(\d+)\D*$/)?.[1] ?? 0), now: /^stata\s*now/i.test(name) };
}

/** A Stata directory as an installation: its editions and licence (usable only with both). */
function describeStata(context: DiscoveryContext, dir: string, name: string, source: RuntimeSource): RuntimeInstallation {
    const { path, exists } = helpers(context);
    const { version, now } = stataRank(name);
    const editions = stataLibraries(context.platform).filter(({ file }) => exists(path.join(dir, file))).map(({ edition }) => edition);
    const licensed = exists(path.join(dir, 'stata.lic'));
    const problem = editions.length === 0 ? 'it has no Stata shared library (Stata 17 or newer has one)'
        : !licensed ? 'it has no licence (stata.lic); Stata will not start without one' : undefined;
    return {
        home: dir,
        ...(version ? { version: String(version) } : {}),
        label: `${now ? 'StataNow' : 'Stata'}${version ? ` ${version}` : ''}`,
        source,
        usable: !problem,
        ...(problem ? { problem } : {}),
        editions,
        licensed
    };
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

/** What findRuntime() is asked for. */
export interface RuntimeChoice {
    /**
     * The installation the user chose (a setting): R's home; Python's installation folder or interpreter; Stata's
     * directory. It is the one returned even when it is too old or not usable -- the caller says why rather than
     * silently using another -- and undefined when it is not an installation of that runtime at all.
     */
    home?: string;
    /** The oldest version wanted ("4.1.0", "3.10"). */
    minVersion?: string;
}

/**
 * The installation of R, Python or Stata to use. With `home`, that one (see RuntimeChoice). Without, the first
 * the list*Installations() function gives that is usable and at least `minVersion`; when none is, the first found
 * (its `usable` / `meetsMinimum` say why it can't be used); undefined when there is none at all.
 */
export async function findRuntime(kind: 'r' | 'python' | 'stata', choice: RuntimeChoice = {}, context: DiscoveryContext = defaultContext()): Promise<FoundRuntime | undefined> {
    const meets = (installation: RuntimeInstallation): FoundRuntime => ({
        ...installation,
        meetsMinimum: !choice.minVersion || !installation.version || compareVersions(installation.version, choice.minVersion) >= 0
    });
    if (choice.home) {
        const chosen = await describeHome(kind, choice.home, context);
        return chosen && meets(chosen);
    }
    const list = kind === 'r' ? await listRInstallations(context) : kind === 'python' ? await listPythonInstallations(context) : await listStataInstallations(context);
    const found = list.map(meets);
    return found.find((installation) => installation.usable && installation.meetsMinimum) ?? found[0];
}

/** The installation at a path the user gave, or undefined when there is none of that runtime there. */
async function describeHome(kind: 'r' | 'python' | 'stata', home: string, context: DiscoveryContext): Promise<RuntimeInstallation | undefined> {
    const { path, exists } = helpers(context);
    if (kind === 'r') {
        return exists(path.join(home, 'library', 'base')) ? describeR(context, home, 'setting') : undefined;
    }
    if (kind === 'python') {
        for (const executable of pythonExecutablesIn(context, home).filter(exists)) {
            const answer = parsePythonAnswer(await context.run(executable, ['-c', PYTHON_SCRIPT]), 'setting');
            if (answer && !isStoreAlias(answer.executable)) return describePython(answer);
        }
        return undefined;
    }
    const stata = describeStata(context, home.replace(/[\\/]+$/, ''), path.basename(home.replace(/[\\/]+$/, '')), 'setting');
    return stata.editions!.length > 0 ? stata : undefined;
}

/** The Rscript of an R home (bin/Rscript, or bin/x64 on older Windows R), if there is one. */
export function findRscript(rHome: string, platform: string = process.platform): string | undefined {
    const path = platform === 'win32' ? win32 : posix;
    const name = platform === 'win32' ? 'Rscript.exe' : 'Rscript';
    return [path.join(rHome, 'bin', name), ...(platform === 'win32' ? [path.join(rHome, 'bin', 'x64', name)] : [])].find((p) => existsSync(p));
}

/**
 * R's own libraries other than its base one -- the user library and any site library -- as `.libPaths()` has them
 * in a plain R of that installation (R_LIBS and R_LIBS_USER of this process are not passed on, so it is R's own
 * answer). A caller that puts its own library first lists these after it, so the user's packages stay visible.
 */
export async function readRLibraries(rHome: string): Promise<string[]> {
    const rscript = findRscript(rHome);
    if (!rscript) throw new Error(`There is no Rscript in ${rHome}`);
    const env = { ...process.env };
    delete env.R_LIBS;
    delete env.R_LIBS_USER;
    const output = await new Promise<string>((resolve, reject) => {
        execFile(rscript, ['-e', 'cat(setdiff(normalizePath(.libPaths(), "/"), normalizePath(.Library, "/")), sep = "\\n")'],
            { env, encoding: 'utf8', timeout: 30_000, windowsHide: true }, (error, stdout) => (error ? reject(error) : resolve(stdout)));
    });
    return output.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
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
