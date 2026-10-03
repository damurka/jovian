// Installs or updates an R package and what its dependency tree needs. The work is hera's .jv.pkg.ensure()
// (packages/hera/R/packages-ensure.R), run in a packages session of the session manager's -- an R session of its own,
// never a user's, which may have the very packages being replaced loaded (on Windows a loaded package's DLL can't be
// replaced). It stays warm between installs, so a check costs a function call, not an R start-up. The session manager
// knows its other sessions: an install that replaces packages waits for those using the library, and new sessions on
// it wait for the install. What an application decides -- the oldest R it supports, which library its packages go
// into -- it passes in.
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

/** What to install: SessionManager.ensureRPackage()'s request. */
export interface EnsureRPackageRequest {
    name: string;
    /** The oldest version that will do; installed or updated when what is there is older. */
    minVersion?: string;
    /** Repositories to look in before CRAN, e.g. an r-universe (`https://<owner>.r-universe.dev`). */
    repos?: readonly string[];
    /**
     * Also bring the package, and the dependencies that come from `repos`, up to the newest version available.
     * Without it nothing is downloaded when `minVersion` is already met.
     */
    update?: boolean;
    /**
     * Also install the suggested packages of the packages from `repos` (what their code uses when it is there).
     * Default true. Without them a fresh install finishes much sooner, so they can be left for a second run.
     */
    optional?: boolean;
}

/**
 * How far an install has got. `waiting`: another process is installing into the same library; `checking`: looking
 * in the repos for what is needed; `downloading` the `total` packages; `installing` them, `done` so far;
 * `retrying` downloads that broke off.
 */
export interface RPackageProgress {
    phase: 'waiting' | 'checking' | 'downloading' | 'installing' | 'retrying';
    done?: number;
    total?: number;
    /** The package being installed, or the last one installed. */
    current?: string;
    /** While `downloading` or `retrying`: how much of the packages has arrived so far, in bytes. */
    bytes?: number;
}

export interface RPackageResult {
    /** The version installed before, if any. */
    previousVersion?: string;
    /** The version installed now. */
    version?: string;
    /** The packages this run installed or updated. */
    installed: string[];
    /** A repo could not be reached and what is installed was kept, unchecked against the repos. */
    offline: boolean;
}

/** The name of the error ensureRPackage() rejects with when the repos can't be reached and what is installed won't do. */
export const R_PACKAGES_OFFLINE = 'RPackagesOffline';

export interface EnsureRPackageOptions {
    /** The R installation. */
    rHome: string;
    /**
     * The libraries R sees, in order (R_LIBS): packages are installed into the first. Without, R's own
     * (.libPaths()[1], usually the user library).
     */
    libraries?: readonly string[];
    /** The oldest R the packages may be installed for; an older R fails with a message saying so. */
    minRVersion?: string;
    /**
     * A DESCRIPTION field (e.g. "Config/myapp/onDemand") in which a package from `repos` names suggested packages
     * to leave out: ones its users install only when they ask for the feature that needs them.
     */
    onDemandField?: string;
    /** The install's progress lines, for a log. */
    onOutput?: (line: string) => void;
    /** How far the install has got, for people. */
    onProgress?: (progress: RPackageProgress) => void;
    /** Default 30 minutes. */
    timeoutMs?: number;
    /**
     * Before an install that replaces packages already installed, wait while other sessions of this manager use the
     * library -- they may have them loaded, and on Windows a loaded package can't be replaced (default true).
     * Installing only missing packages never waits.
     */
    waitForSessions?: boolean;
}

const R_PACKAGE = `([A-Za-z][A-Za-z0-9.]*)`;
/** R's lines saying a package is installed: a Windows binary unpacked, a source (or Linux binary) package built. */
const R_INSTALLED = [new RegExp(`^package [\u2018'"]${R_PACKAGE}[\u2019'"] successfully unpacked`), new RegExp(`^\\* DONE \\(${R_PACKAGE}\\)`)];
/** R's lines saying a package is being built. */
const R_BUILDING = [new RegExp(`^begin installing package [\u2018'"]${R_PACKAGE}[\u2019'"]`), new RegExp(`^\\* installing \\*(?:source|binary)\\* package [\u2018'"]${R_PACKAGE}[\u2019'"]`)];


/**
 * Follows an install's output -- the progress .jv.pkg.ensure() prints (without its `JOVIAN_PKG: `) and the lines
 * install.packages() prints per package -- into how far it has got. Returns the function to give each line to.
 */
export function followRInstall(onStatus: (status: RPackageProgress) => void): (line: string, fromScript: boolean) => void {
    let total = 0;
    const installed = new Set<string>();
    return (line, fromScript) => {
        if (fromScript) {
            if (line.startsWith('Checking ')) {
                onStatus({ phase: 'checking' });
            } else if (line.startsWith('Installing ')) {
                total = line.slice('Installing '.length).split(', ').length;
                onStatus({ phase: 'downloading', done: 0, total });
            } else if (line.startsWith('Some downloads did not finish')) {
                onStatus({ phase: 'retrying', done: installed.size, total });
            }
            return;
        }
        for (const pattern of R_INSTALLED) {
            const match = pattern.exec(line);
            if (match && !installed.has(match[1])) {
                installed.add(match[1]);
                onStatus({ phase: 'installing', done: Math.min(installed.size, total), total, current: match[1] });
                return;
            }
        }
        for (const pattern of R_BUILDING) {
            const match = pattern.exec(line);
            if (match) {
                onStatus({ phase: 'installing', done: Math.min(installed.size, total), total, current: match[1] });
                return;
            }
        }
    };
}

/** The lock file in a library while a process installs into it (see {@link lockLibrary}). */
export const LIBRARY_LOCK_FILE = '.jovian-install.lock';

/** Whether a process is running: one of another user (an elevated application) counts too. */
function isRunning(pid: number): boolean {
    try {
        process.kill(pid, 0);
        return true;
    } catch (error) {
        return (error as NodeJS.ErrnoException).code === 'EPERM';
    }
}

/**
 * Takes the library for one install, across processes. Within one, installs should run one at a time; but two
 * applications can run at once (one elevated, one not, can't see each other) and both install into the same library
 * -- and R locks the whole library while it unpacks a Windows package, so the second failed half way ("failed to lock
 * directory ... 00LOCK"). The lock file holds its owner's pid; one whose process is gone, or older than `staleMs`, is
 * a crashed install's and is taken over. Resolves with the function that gives the library back.
 */
export async function lockLibrary(library: string, staleMs: number, onWait: () => void): Promise<() => Promise<void>> {
    await fs.promises.mkdir(library, { recursive: true });
    const lockFile = join(library, LIBRARY_LOCK_FILE);
    let waited = false;
    for (; ;) {
        try {
            await fs.promises.writeFile(lockFile, JSON.stringify({ pid: process.pid, at: Date.now() }), { flag: 'wx' });
            return () => fs.promises.rm(lockFile, { force: true });
        } catch (error) {
            if ((error as NodeJS.ErrnoException).code !== 'EEXIST') {
                throw error;
            }
        }
        let holder: { pid?: unknown; at?: unknown } | undefined;
        try {
            holder = JSON.parse(await fs.promises.readFile(lockFile, 'utf8'));
        } catch {
            // just created and not written yet, or gone since: look again
        }
        const stat = await fs.promises.stat(lockFile).catch(() => undefined);
        const age = Date.now() - (typeof holder?.at === 'number' ? holder.at : stat?.mtimeMs ?? Date.now());
        const gone = typeof holder?.pid === 'number' && !isRunning(holder.pid);
        if (stat && (gone || age > staleMs)) {
            await fs.promises.rm(lockFile, { force: true });
            continue;
        }
        if (!waited) {
            waited = true;
            onWait();
        }
        await sleep(2000);
    }
}

/**
 * R's own locks left by an install that stopped half way (the application closed during it, a crash): `00LOCK` (the whole
 * library) and `00LOCK-<package>`. R refuses to install while they are there, and says to remove them. Called with
 * the library taken (lockLibrary), so no other install is running to own them.
 */
async function removeStaleRLocks(library: string): Promise<void> {
    const names = await fs.promises.readdir(library).catch(() => [] as string[]);
    await Promise.all(names.filter(name => name === '00LOCK' || name.startsWith('00LOCK-'))
        .map(name => fs.promises.rm(join(library, name), { recursive: true, force: true })));
}

/** What ensureRPackageIn() needs of a session: to run code and say what it printed. */
export interface RPackagesSession {
    execute(code: string, options?: { timeout?: number }): Promise<{ success: boolean; error?: { message: string } | undefined }>;
    on(event: 'message', listener: (message: { msgType: string; content: unknown }) => void): unknown;
    off(event: 'message', listener: (message: { msgType: string; content: unknown }) => void): unknown;
}

/** What ensureRPackageIn() needs of the session manager. */
export interface RPackagesHost {
    /** The packages session for this R and these libraries, started if need be. */
    packagesSession(rHome: string, libraries: readonly string[]): Promise<RPackagesSession>;
    /** The other sessions (not packages sessions) using `library`: their ids, for the progress and the log. */
    sessionsUsing(library: string): string[];
    /** New R sessions on `library` wait until `until` settles. */
    hold(library: string, until: Promise<unknown>): void;
}

/** A string as an R literal (JSON's escapes are a subset of R's). */
const literal = (value: string) => JSON.stringify(value);

/** The parsed lines of a .jv.pkg.ensure() run. */
interface RunOutcome {
    failure?: string;
    offline?: string;
    result?: RPackageResult;
    /** plan_only: what would be installed, and which of those are installed already. */
    plan?: { install: string[]; replace: string[] };
    /** What else it printed: R's output, for an error without a reason. */
    other: string[];
}

/** Runs .jv.pkg.ensure() in the session, following its lines (each also to `onLine`) until it returns. */
async function runEnsure(session: RPackagesSession, call: string, timeoutMs: number, onLine: (line: string, fromScript: boolean) => void): Promise<RunOutcome> {
    const outcome: RunOutcome = { other: [] };
    let partial = '';
    const take = (line: string) => {
        if (line.startsWith('JOVIAN_PKG_ERROR: ')) {
            outcome.failure = line.slice('JOVIAN_PKG_ERROR: '.length);
        } else if (line.startsWith('JOVIAN_PKG_OFFLINE: ')) {
            outcome.offline = line.slice('JOVIAN_PKG_OFFLINE: '.length);
        } else if (line.startsWith('JOVIAN_PKG_PLAN: ')) {
            const [install, replace] = line.slice('JOVIAN_PKG_PLAN: '.length).split(' ');
            const list = (value: string | undefined) => value && value !== '-' ? value.split(',') : [];
            outcome.plan = { install: list(install), replace: list(replace) };
        } else if (line.startsWith('JOVIAN_PKG_RESULT: ')) {
            const [before, after, installed, reached] = line.slice('JOVIAN_PKG_RESULT: '.length).split(' ');
            outcome.result = {
                ...(before && before !== 'NA' ? { previousVersion: before } : {}),
                ...(after && after !== 'NA' ? { version: after } : {}),
                installed: installed && installed !== '-' ? installed.split(',') : [],
                offline: reached === 'offline'
            };
        } else if (line.startsWith('JOVIAN_PKG: ')) {
            onLine(line.slice('JOVIAN_PKG: '.length), true);
        } else if (line.trim()) {
            onLine(line.trim(), false);
            outcome.other.push(line.trim());
            if (outcome.other.length > 400) outcome.other.splice(0, 200);
        }
    };
    const listener = (message: { msgType: string; content: unknown }) => {
        if (message.msgType !== 'stream') return;
        const text = (message.content as { text?: unknown } | undefined)?.text;
        if (typeof text !== 'string') return;
        const lines = (partial + text).split(/\r?\n/);
        partial = lines.pop() ?? '';
        lines.forEach(take);
    };
    session.on('message', listener);
    try {
        const ran = await session.execute(call, { timeout: timeoutMs });
        if (partial) take(partial);
        if (!ran.success && !outcome.failure && !outcome.offline) {
            outcome.failure = ran.error?.message ?? 'the install stopped';
        }
    } finally {
        session.off('message', listener);
    }
    return outcome;
}

/**
 * Installs or updates an R package, and whatever of its dependency tree it needs, into the first of
 * `options.libraries`, in the host's packages session (SessionManager.ensureRPackage() is the way in). One install at a
 * time per library, across processes ({@link lockLibrary}). When it would replace packages already installed (an
 * update), it first waits for the host's other sessions using the library (`waitForSessions`), and new R sessions on
 * the library wait for it. Rejects with R's explanation when the install fails, and with an error named
 * {@link R_PACKAGES_OFFLINE} when the repos can't be reached and what is installed won't do.
 */
export async function ensureRPackageIn(host: RPackagesHost, request: EnsureRPackageRequest, options: EnsureRPackageOptions): Promise<RPackageResult> {
    const libraries = (options.libraries ?? []).filter((library) => library.length > 0);
    const library = libraries[0];
    const timeoutMs = options.timeoutMs ?? 30 * 60 * 1000;
    const unlock = library
        ? await lockLibrary(library, timeoutMs + 5 * 60 * 1000, () => {
            options.onOutput?.('Waiting for another process to finish installing R packages into the library');
            options.onProgress?.({ phase: 'waiting' });
        })
        : undefined;
    let downloads: string | undefined;
    try {
        if (library) {
            await removeStaleRLocks(library);
        }
        const session = await host.packagesSession(options.rHome, libraries);
        downloads = await fs.promises.mkdtemp(join(tmpdir(), 'jovian-r-downloads-'));
        const call = (planOnly: boolean) => 'base::as.environment("tools:jovian")$.jv.pkg.ensure(' + [
            literal(request.name),
            `min_version = ${literal(request.minVersion ?? '')}`,
            `update = ${request.update ? 'TRUE' : 'FALSE'}`,
            `optional = ${request.optional === false ? 'FALSE' : 'TRUE'}`,
            `repos = c(${(request.repos ?? []).map(literal).join(', ')})`,
            `min_r = ${literal(options.minRVersion ?? '')}`,
            `on_demand_field = ${literal(options.onDemandField ?? '')}`,
            `downloads = ${literal(downloads!.replace(/\\/g, '/'))}`,
            `plan_only = ${planOnly ? 'TRUE' : 'FALSE'}`
        ].join(', ') + ')';

        let status: RPackageProgress | undefined;
        const follow = followRInstall((next) => {
            status = next;
            options.onProgress?.(next);
        });
        const onLine = (line: string, fromScript: boolean) => {
            if (fromScript) options.onOutput?.(line);
            follow(line, fromScript);
        };

        // What it would replace: an update of packages other sessions on the library may have loaded waits for them
        let replacing: string[] = [];
        if (library && options.waitForSessions !== false) {
            const plan = await runEnsure(session, call(true), timeoutMs, onLine);
            if (plan.failure || plan.offline || plan.result) {
                // nothing to install (plan.result), or why it can't be: the same outcome as the install itself
                return outcomeOf(plan, request);
            }
            replacing = plan.plan?.replace ?? [];
            for (let waited = false; replacing.length > 0 && host.sessionsUsing(library).length > 0; waited = true) {
                if (!waited) {
                    const using = host.sessionsUsing(library);
                    options.onOutput?.(`Waiting for ${using.length} session${using.length === 1 ? '' : 's'} using the library to end before replacing ${replacing.join(', ')}`);
                    options.onProgress?.({ phase: 'waiting' });
                }
                await sleep(2000);
            }
        }

        // R downloads every package at once and says nothing until all have arrived: while it does, the size of what
        // has arrived is what shows the install is moving
        let shownBytes = 0;
        const isDownloading = () => status?.phase === 'downloading' || status?.phase === 'retrying';
        const watch = setInterval(async () => {
            if (!isDownloading()) return;
            const bytes = await folderSize(downloads!);
            if (bytes !== shownBytes && isDownloading()) {
                shownBytes = bytes;
                options.onProgress?.({ ...status!, bytes });
            }
        }, 1000);
        const install = runEnsure(session, call(false), timeoutMs, onLine).finally(() => clearInterval(watch));
        if (library && replacing.length > 0) {
            // no session starts on the library half way through replacing its packages
            host.hold(library, install);
        }
        return outcomeOf(await install, request);
    } finally {
        if (downloads) {
            await fs.promises.rm(downloads, { recursive: true, force: true }).catch(() => undefined);
        }
        await unlock?.();
    }
}

/** A run's result, or the error it ended with. */
function outcomeOf(outcome: RunOutcome, request: EnsureRPackageRequest): RPackageResult {
    if (outcome.offline) {
        const error = new Error(outcome.offline);
        error.name = R_PACKAGES_OFFLINE;
        throw error;
    }
    if (outcome.failure || !outcome.result) {
        // R's error and what followed it, rather than the end of a compiler's output
        const errorAt = outcome.other.findIndex((line) => /^Error\b/.test(line));
        const why = (errorAt >= 0 ? outcome.other.slice(errorAt) : outcome.other).slice(-5);
        throw new Error(outcome.failure ?? `Installing ${request.name} failed${why.length ? `: ${why.join(' | ')}` : ''}`);
    }
    return outcome.result;
}

/** The total size of the files in a folder (not its subfolders), in bytes; what can't be read counts as nothing. */
export async function folderSize(folder: string): Promise<number> {
    const entries = await fs.promises.readdir(folder, { withFileTypes: true }).catch(() => []);
    const sizes = await Promise.all(entries.filter(entry => entry.isFile())
        .map(entry => fs.promises.stat(join(folder, entry.name)).then(stat => stat.size, () => 0)));
    return sizes.reduce((sum, size) => sum + size, 0);
}
