// Installs Python packages into a virtual environment, with pip, in a process
// of its own -- never in a session's kernel, which may have the very packages
// being replaced loaded. The environment is the caller's: one per application
// and Python version, made (or re-made, for another Python) by
// ensurePythonEnvironment(). Through the session manager
// (SessionManager.ensurePythonPackages()), an install that would replace
// packages waits for the sessions using the environment (their venvPath), and
// new sessions on it wait for the install, as for R (r-packages.ts).
import { execFile, spawn } from 'node:child_process';
import * as fs from 'node:fs';
import { dirname, join } from 'node:path';
import { createInterface } from 'node:readline';
import { setTimeout as sleep } from 'node:timers/promises';
import { lockLibrary, packagesInUse, type WhenInUse } from './r-packages.js';

/** What to install: {@link ensurePythonPackages}' request. */
export interface PythonPackageRequest {
    /** An app's folder: its `requirements.txt`, else the dependencies in its `pyproject.toml`, are installed. */
    appDir?: string;
    /** A pip package the app runs from, e.g. `mypkg`. */
    name?: string;
    /** The oldest version of {@link name} that will do. */
    minVersion?: string;
    /** A package index to look in besides PyPI (pip's `--extra-index-url`). */
    index?: string;
    /** Requirements, e.g. `shiny>=1.0`, or `plotly` for a module an app turned out to need. */
    requirements?: readonly string[];
    /** Also bring {@link name} up to the newest version available. Without it nothing is downloaded when every requirement is met. */
    update?: boolean;
}

export interface PythonPackageResult {
    /** The version of {@link PythonPackageRequest.name} installed before, if any. */
    previousVersion?: string;
    version?: string;
    /** Whether pip installed or updated anything. */
    changed: boolean;
    /** The package index could not be reached, but what is installed meets every requirement, so it stays in use. */
    offline?: boolean;
}

/** The name of the error ensurePythonPackages() rejects with when the index can't be reached and what is installed won't do. */
export const PYTHON_PACKAGES_OFFLINE = 'PythonPackagesOffline';

export interface PythonPackageOptions {
    /** The progress lines, for a log. */
    onOutput?: (line: string) => void;
    /** Default 30 minutes. */
    timeoutMs?: number;
    /**
     * SessionManager.ensurePythonPackages(): an install that replaces packages already installed while the manager's
     * sessions use the environment -- `wait` for them to end (the default), `defer` (reject with an error named
     * PACKAGES_IN_USE, installing nothing) or `proceed`. Installing only missing packages never waits.
     */
    whenInUse?: WhenInUse;
}

/**
 * Installs what a Python app needs into the virtual environment it runs in,
 * with pip. Run by the venv's python (`python -c`) with one argument, a JSON string holding
 * `{requirements, requirementsFile, pyproject, package, index, update}`.
 *
 * Every requirement already met means nothing is downloaded (unless `update`),
 * so it is cheap to run before every launch and works offline once set up.
 * Otherwise pip installs what is missing or too old; with `update`, the app's
 * package is also brought up to its newest version (its dependencies only as
 * far as it needs: pip's only-if-needed strategy).
 *
 * Talks back on stdout: `JOVIAN_PY: <progress>`, `JOVIAN_PY_ERROR: <why>`,
 * `JOVIAN_PY_OFFLINE: <why>` (the index could not be reached and what is
 * installed won't do) and, on success, `JOVIAN_PY_RESULT: <json>` with
 * `{previous, version, changed, offline}`.
 */
const PY_INSTALL_SCRIPT = `
import json, os, subprocess, sys
import importlib.metadata as md
from pip._vendor.packaging.requirements import Requirement

def say(*parts):
    print("JOVIAN_PY: " + " ".join(str(p) for p in parts), flush=True)

def fail(msg, kind="ERROR"):
    print("JOVIAN_PY_" + kind + ": " + " ".join(str(msg).split()), flush=True)
    sys.exit(1)

args = json.loads(sys.argv[1])
reqs = list(args.get("requirements") or [])
opaque = False

req_file = args.get("requirementsFile")
if req_file:
    for line in open(req_file, encoding="utf-8"):
        line = line.split(" #")[0].strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("-"):
            opaque = True
        else:
            reqs.append(line)

pyproject = args.get("pyproject")
if pyproject:
    try:
        import tomllib
    except ImportError:
        fail("Reading pyproject.toml needs Python 3.11 or newer: use requirements.txt for this app, or a newer Python.")
    with open(pyproject, "rb") as f:
        reqs.extend(tomllib.load(f).get("project", {}).get("dependencies", []))

pkg = args.get("package")
pkg_req = None
if pkg:
    pkg_req = pkg["name"] + (">=" + pkg["version"] if pkg.get("version") else "")
    reqs.append(pkg_req)

def version_of(name):
    try:
        return md.version(name)
    except md.PackageNotFoundError:
        return None

def unmet(requirements):
    missing = []
    for text in requirements:
        try:
            req = Requirement(text)
        except Exception:
            missing.append(text)
            continue
        if req.marker is not None and not req.marker.evaluate():
            continue
        have = version_of(req.name)
        if have is None or (req.specifier and not req.specifier.contains(have, prereleases=True)):
            missing.append(text)
    return missing

previous = version_of(pkg["name"]) if pkg else None
update = bool(args.get("update"))
todo = unmet(reqs)
if not todo and not opaque and not update:
    print("JOVIAN_PY_RESULT: " + json.dumps({"previous": previous, "version": previous, "changed": False, "offline": False}), flush=True)
    sys.exit(0)

if args.get("planOnly"):
    # what pip would install, and which of those are installed already (would be replaced): pip's own resolver, without
    # installing (pip 22.2 or newer); else, when it can't say (older pip, offline), every unmet requirement installed already
    names = list(todo) + ([pkg_req] if update and pkg_req and pkg_req not in todo else [])
    cmd = [sys.executable, "-m", "pip", "install", "--dry-run", "--quiet", "--report", "-", "--disable-pip-version-check", "--no-input",
        "--timeout", "30", "--retries", "2", "--upgrade-strategy", "only-if-needed"] + (["--upgrade"] if update and pkg_req else [])
    if args.get("index"):
        cmd += ["--extra-index-url", args["index"]]
    say("Checking what " + ", ".join(names + (["-r " + os.path.basename(req_file)] if opaque else [])) + " would install")
    proc = subprocess.run(cmd + names + (["-r", req_file] if opaque else []), capture_output=True, text=True, encoding="utf-8", errors="replace")
    try:
        install = [item["metadata"]["name"] for item in json.loads(proc.stdout).get("install", [])]
    except Exception:
        install = []
        for text in names:
            try:
                install.append(Requirement(text).name)
            except Exception:
                pass
    replace = [name for name in install if version_of(name) is not None]
    print("JOVIAN_PY_PLAN: " + json.dumps({"install": install, "replace": replace}), flush=True)
    sys.exit(0)

NETWORK = ("NewConnectionError", "Failed to establish a new connection", "getaddrinfo failed", "Name or service not known",
    "Temporary failure in name resolution", "Could not fetch URL", "ConnectTimeoutError", "ReadTimeoutError", "No route to host", "ProxyError")

def pip(extra, label):
    cmd = [sys.executable, "-m", "pip", "install", "--disable-pip-version-check", "--no-input", "--timeout", "30", "--retries", "2", "--upgrade-strategy", "only-if-needed"]
    if args.get("index"):
        cmd += ["--extra-index-url", args["index"]]
    say(label)
    proc = subprocess.run(cmd + extra, capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, out

changed = False
problems = []
network = False
unreached = False
if todo or opaque:
    # a requirements file with options (-r, -e, --index-url...) is handed to pip whole
    extra = list(todo) + (["-r", req_file] if opaque else [])
    code, out = pip(extra, "Installing " + ", ".join(todo + (["-r " + os.path.basename(req_file)] if opaque else [])))
    changed = changed or "Successfully installed" in out
    if code != 0:
        network = network or any(n in out for n in NETWORK)
        problems.append(out.strip().splitlines()[-1] if out.strip() else "pip exited with code %d" % code)
if update and pkg_req and not problems:
    code, out = pip(["--upgrade", pkg_req], "Checking for " + pkg["name"] + " updates")
    changed = changed or "Successfully installed" in out
    # pip keeps what is installed, and exits 0, when it can't reach the index for an upgrade
    unreached = code == 0 and any(n in out for n in NETWORK)
    if code != 0:
        network = network or any(n in out for n in NETWORK)
        problems.append(out.strip().splitlines()[-1] if out.strip() else "pip exited with code %d" % code)

still = unmet(reqs)
version = version_of(pkg["name"]) if pkg else None
if not still and (not problems or network):
    print("JOVIAN_PY_RESULT: " + json.dumps({"previous": previous, "version": version, "changed": changed, "offline": bool(problems) or unreached}), flush=True)
    sys.exit(0)
if network:
    fail("Could not reach the Python package index to install " + ", ".join(still or todo) + ".", "OFFLINE")
fail("Could not install " + ", ".join(still or todo) + ". " + " ".join(problems))
`;

/** The python inside a virtual environment. */
export function venvPython(venvDir: string, platform: string = process.platform): string {
    return platform === 'win32' ? join(venvDir, 'Scripts', 'python.exe') : join(venvDir, 'bin', 'python');
}

/** A virtual environment's site-packages for a Python version (`3.12.10`), where its packages are installed. */
export function venvSitePackages(venvDir: string, version: string, platform: string = process.platform): string {
    const [major, minor] = version.split('.');
    return platform === 'win32' ? join(venvDir, 'Lib', 'site-packages') : join(venvDir, 'lib', `python${major}.${minor}`, 'site-packages');
}

/** The `home` a venv's pyvenv.cfg records: the folder of the python it was made from. */
export function readVenvHome(cfg: string): string | undefined {
    return /^\s*home\s*=\s*(.+?)\s*$/m.exec(cfg)?.[1];
}

/**
 * Creates the virtual environment at `venvDir` with the Python whose interpreter is `pythonExecutable` when it is
 * missing, or re-creates it when it was made from another Python (the user installed a different one of the same
 * version, or moved it): a venv only works with the python it came from.
 */
export async function ensurePythonEnvironment(pythonExecutable: string, venvDir: string, onOutput?: (line: string) => void): Promise<void> {
    const state = await environmentState(pythonExecutable, venvDir);
    if (state !== 'ready') {
        await makeEnvironment(pythonExecutable, venvDir, state === 'other', onOutput);
    }
}

/**
 * {@link ensurePythonEnvironment}, with the session manager's sessions in mind (SessionManager.ensurePythonEnvironment()
 * is the way in): re-creating an environment made from another Python empties it, so it first waits while the host's
 * sessions use it, and new sessions on it wait until it is made.
 */
export async function ensurePythonEnvironmentIn(host: PythonPackagesHost, pythonExecutable: string, venvDir: string, onOutput?: (line: string) => void): Promise<void> {
    const state = await environmentState(pythonExecutable, venvDir);
    if (state === 'ready') {
        return;
    }
    if (state === 'other') {
        for (let waited = false; ; waited = true) {
            const using = await host.sessionsUsing(venvDir);
            if (using.length === 0) break;
            if (!waited) {
                onOutput?.(`Waiting for ${using.length} session${using.length === 1 ? '' : 's'} using ${venvDir} to end before re-creating it for ${pythonExecutable}`);
            }
            await sleep(2000);
        }
    }
    const making = makeEnvironment(pythonExecutable, venvDir, state === 'other', onOutput);
    host.hold(venvDir, making);
    await making;
}

/** Whether the environment at `venvDir` is there for this Python (`ready`), missing, or made from another Python. */
async function environmentState(pythonExecutable: string, venvDir: string): Promise<'ready' | 'missing' | 'other'> {
    let home: string | undefined;
    try {
        home = readVenvHome(await fs.promises.readFile(join(venvDir, 'pyvenv.cfg'), 'utf8'));
    } catch { /* no venv yet */ }
    if (!home) {
        return 'missing';
    }
    return fs.existsSync(venvPython(venvDir)) && samePath(home, dirname(pythonExecutable)) ? 'ready' : 'other';
}

/** Makes the environment with `python -m venv`; `clear` empties one made from another Python first. */
async function makeEnvironment(pythonExecutable: string, venvDir: string, clear: boolean, onOutput?: (line: string) => void): Promise<void> {
    onOutput?.(clear ? `Re-creating ${venvDir} for ${pythonExecutable}` : `Creating ${venvDir}`);
    await fs.promises.mkdir(dirname(venvDir), { recursive: true });
    await new Promise<void>((resolve, reject) => {
        execFile(pythonExecutable, ['-m', 'venv', ...(clear ? ['--clear'] : []), venvDir], { windowsHide: true, timeout: 5 * 60 * 1000 }, (error, _stdout, stderr) => {
            if (error) {
                reject(new Error(`Could not create a Python virtual environment with ${pythonExecutable}${stderr ? `: ${String(stderr).trim().split(/\r?\n/).pop()}` : ''} (on Debian/Ubuntu, install python3-venv).`));
            } else {
                resolve();
            }
        });
    });
}

function samePath(a: string, b: string): boolean {
    const norm = (p: string) => p.replace(/[\\/]+/g, '/').replace(/\/$/, '');
    return process.platform === 'win32' || process.platform === 'darwin' ? norm(a).toLowerCase() === norm(b).toLowerCase() : norm(a) === norm(b);
}

/** The arguments the install script reads, from a request. */
export function buildInstallArgs(request: PythonPackageRequest, appFiles: { requirementsFile?: string; pyproject?: string }): object {
    return {
        requirements: [...(request.requirements ?? [])],
        requirementsFile: appFiles.requirementsFile,
        // requirements.txt wins: an app with both lists its runtime needs there
        pyproject: appFiles.requirementsFile ? undefined : appFiles.pyproject,
        package: request.name ? { name: request.name, version: request.minVersion } : undefined,
        index: request.index,
        update: !!request.update,
    };
}

/** The app folder's `requirements.txt` and `pyproject.toml`, where they exist. */
export function findAppRequirementFiles(appDir: string | undefined): { requirementsFile?: string; pyproject?: string } {
    if (!appDir) {
        return {};
    }
    const requirementsFile = join(appDir, 'requirements.txt');
    const pyproject = join(appDir, 'pyproject.toml');
    return {
        requirementsFile: fs.existsSync(requirementsFile) ? requirementsFile : undefined,
        pyproject: fs.existsSync(pyproject) ? pyproject : undefined,
    };
}

/** What one run of {@link PY_INSTALL_SCRIPT} said. */
export interface PythonInstallRun {
    failure?: string;
    offline?: string;
    result?: PythonPackageResult;
    /** planOnly: what pip would install, and which of those are installed already. */
    plan?: { install: string[]; replace: string[] };
    /** What else it printed (pip's output), for an error without a reason. */
    other: string[];
    /** python's exit code; null when it was stopped. */
    code: number | null;
    timedOut?: boolean;
}

/** Runs {@link PY_INSTALL_SCRIPT} with the venv's python and the arguments (JSON), following its lines. */
export type PythonInstallRunner = (python: string, args: string, timeoutMs: number, onOutput: (line: string) => void) => Promise<PythonInstallRun>;

/** Runs the install script in a process of its own: `python -c`, its arguments one JSON argument -- no file is written. */
export const runPythonInstallScript: PythonInstallRunner = async (python, args, timeoutMs, onOutput) => {
    const run: PythonInstallRun = { other: [], code: null };
    const env = { ...process.env, PYTHONNOUSERSITE: '1', PIP_DISABLE_PIP_VERSION_CHECK: '1', PYTHONIOENCODING: 'utf-8' };
    const child = spawn(python, ['-c', PY_INSTALL_SCRIPT, args], { env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
    for (const stream of [child.stdout, child.stderr]) {
        createInterface({ input: stream }).on('line', line => {
            if (line.startsWith('JOVIAN_PY_ERROR: ')) {
                run.failure = line.slice('JOVIAN_PY_ERROR: '.length);
            } else if (line.startsWith('JOVIAN_PY_OFFLINE: ')) {
                run.offline = line.slice('JOVIAN_PY_OFFLINE: '.length);
            } else if (line.startsWith('JOVIAN_PY_PLAN: ')) {
                const parsed = JSON.parse(line.slice('JOVIAN_PY_PLAN: '.length)) as { install?: string[]; replace?: string[] };
                run.plan = { install: parsed.install ?? [], replace: parsed.replace ?? [] };
            } else if (line.startsWith('JOVIAN_PY_RESULT: ')) {
                const parsed = JSON.parse(line.slice('JOVIAN_PY_RESULT: '.length)) as { previous?: string | null; version?: string | null; changed?: boolean; offline?: boolean };
                run.result = { previousVersion: parsed.previous ?? undefined, version: parsed.version ?? undefined, changed: !!parsed.changed, offline: !!parsed.offline };
            } else if (line.startsWith('JOVIAN_PY: ')) {
                onOutput(line.slice('JOVIAN_PY: '.length));
            } else if (line.trim()) {
                run.other.push(line.trim());
                if (run.other.length > 400) run.other.splice(0, 200);
            }
        });
    }
    const timer = setTimeout(() => { run.timedOut = true; child.kill(); }, timeoutMs);
    run.code = await new Promise<number | null>((resolve, reject) => {
        child.once('error', error => { clearTimeout(timer); reject(error); });
        child.once('close', exitCode => { clearTimeout(timer); resolve(exitCode); });
    });
    return run;
};

/** A run's result, or the error it ended with. */
function outcomeOf(run: PythonInstallRun, timeoutMs: number): PythonPackageResult {
    if (run.timedOut) {
        throw new Error(`Installing Python packages took longer than ${Math.round(timeoutMs / 60000)} minutes and was stopped.`);
    }
    if (run.offline) {
        const error = new Error(run.offline);
        error.name = PYTHON_PACKAGES_OFFLINE;
        throw error;
    }
    if (run.code !== 0 || !run.result) {
        throw new Error(run.failure ?? `Installing Python packages failed (python exited with code ${run.code})${run.other.length ? `: ${run.other.slice(-5).join(' | ')}` : ''}`);
    }
    return run.result;
}

/**
 * Installs what a request needs into the virtual environment whose python is `venvPythonExecutable` (see
 * {@link venvPython}), with {@link PY_INSTALL_SCRIPT} -- never in a session's kernel, which may have the very packages
 * being replaced loaded. Rejects with pip's explanation when the install fails, and with an error named
 * {@link PYTHON_PACKAGES_OFFLINE} when the index can't be reached and what is installed won't do. It knows nothing of
 * the sessions using the environment: SessionManager.ensurePythonPackages() also waits for them.
 */
export async function ensurePythonPackages(venvPythonExecutable: string, request: PythonPackageRequest, options: PythonPackageOptions = {}): Promise<PythonPackageResult> {
    const timeoutMs = options.timeoutMs ?? 30 * 60 * 1000;
    const args = JSON.stringify(buildInstallArgs(request, findAppRequirementFiles(request.appDir)));
    return outcomeOf(await runPythonInstallScript(venvPythonExecutable, args, timeoutMs, line => options.onOutput?.(line)), timeoutMs);
}

/** What ensurePythonPackagesIn() needs of the session manager. */
export interface PythonPackagesHost {
    /** The sessions using the virtual environment `venvDir` (their venvPath): their ids. */
    sessionsUsing(venvDir: string): Promise<string[]>;
    /** New Python sessions on `venvDir` wait until `until` settles. */
    hold(venvDir: string, until: Promise<unknown>): void;
}

/**
 * Installs what a request needs into the virtual environment `venvDir`, as {@link ensurePythonPackages} does, with the
 * session manager's sessions in mind (SessionManager.ensurePythonPackages() is the way in). One install at a time per
 * environment, across processes. When pip would replace packages already installed (an update, or a requirement
 * raised), it first waits while the host's sessions use the environment, or defers (`whenInUse`) -- on Windows a
 * loaded compiled module (numpy's .pyd) can't be replaced -- and new sessions on it wait for the install.
 */
export async function ensurePythonPackagesIn(host: PythonPackagesHost, venvDir: string, request: PythonPackageRequest,
    options: PythonPackageOptions = {}, run: PythonInstallRunner = runPythonInstallScript): Promise<PythonPackageResult> {
    const timeoutMs = options.timeoutMs ?? 30 * 60 * 1000;
    const onOutput = (line: string) => options.onOutput?.(line);
    const unlock = await lockLibrary(venvDir, timeoutMs + 5 * 60 * 1000,
        () => onOutput('Waiting for another process to finish installing Python packages into the environment'));
    try {
        const args = buildInstallArgs(request, findAppRequirementFiles(request.appDir));
        const python = venvPython(venvDir);

        // What it would replace: an install replacing packages that sessions on the environment may have loaded waits for them
        let replacing: string[] = [];
        const coordinate = options.whenInUse !== 'proceed';
        // No session uses the environment: pip's plan (seconds, on the network) would find no one to wait for. New
        // sessions are kept back for the whole install instead, whatever it replaces.
        const holdAll = coordinate && (await host.sessionsUsing(venvDir)).length === 0;
        if (coordinate && !holdAll) {
            const plan = await run(python, JSON.stringify({ ...args, planOnly: true }), timeoutMs, onOutput);
            if (plan.result || plan.failure || plan.offline || plan.timedOut || !plan.plan) {
                // nothing to install (plan.result), or why it can't be: the same outcome as the install itself
                return outcomeOf(plan, timeoutMs);
            }
            replacing = plan.plan.replace;
            for (let waited = false; replacing.length > 0; waited = true) {
                const using = await host.sessionsUsing(venvDir);
                if (using.length === 0) break;
                if (options.whenInUse === 'defer') {
                    throw packagesInUse(replacing, using);
                }
                if (!waited) {
                    onOutput(`Waiting for ${using.length} session${using.length === 1 ? '' : 's'} using the environment to end before replacing ${replacing.join(', ')}`);
                }
                await sleep(2000);
            }
        }

        const install = run(python, JSON.stringify(args), timeoutMs, onOutput);
        if (replacing.length > 0 || holdAll) {
            // no session starts on the environment half way through replacing its packages
            host.hold(venvDir, install);
        }
        return outcomeOf(await install, timeoutMs);
    } finally {
        await unlock();
    }
}
