// Installs Python packages into a virtual environment, with pip, in a process
// of its own -- never in a session's kernel, which may have the very packages
// being replaced loaded. The environment is the caller's: one per application
// and Python version, made (or re-made, for another Python) by
// ensurePythonEnvironment().
import { execFile, spawn } from 'node:child_process';
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { createInterface } from 'node:readline';

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
    const executable = venvPython(venvDir);
    let home: string | undefined;
    try {
        home = readVenvHome(await fs.promises.readFile(join(venvDir, 'pyvenv.cfg'), 'utf8'));
    } catch { /* no venv yet */ }
    const expected = dirname(pythonExecutable);
    if (home && fs.existsSync(executable) && samePath(home, expected)) {
        return;
    }
    onOutput?.(home ? `Re-creating ${venvDir} for ${pythonExecutable}` : `Creating ${venvDir}`);
    await fs.promises.mkdir(dirname(venvDir), { recursive: true });
    await new Promise<void>((resolve, reject) => {
        execFile(pythonExecutable, ['-m', 'venv', ...(home ? ['--clear'] : []), venvDir], { windowsHide: true, timeout: 5 * 60 * 1000 }, (error, _stdout, stderr) => {
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

/**
 * Installs what a request needs into the virtual environment whose python is `venvPythonExecutable` (see
 * {@link venvPython}), with {@link PY_INSTALL_SCRIPT} -- never in a session's kernel, which may have the very packages
 * being replaced loaded. Rejects with pip's explanation when the install fails, and with an error named
 * {@link PYTHON_PACKAGES_OFFLINE} when the index can't be reached and what is installed won't do.
 */
export async function ensurePythonPackages(venvPythonExecutable: string, request: PythonPackageRequest, options: PythonPackageOptions = {}): Promise<PythonPackageResult> {
    {
        // the installer is code passed to python (-c), its arguments one JSON argument: no file is written
        const args = JSON.stringify(buildInstallArgs(request, findAppRequirementFiles(request.appDir)));

        let failure: string | undefined;
        let offline: string | undefined;
        let result: PythonPackageResult | undefined;
        const otherOutput: string[] = [];
        const env = { ...process.env, PYTHONNOUSERSITE: '1', PIP_DISABLE_PIP_VERSION_CHECK: '1', PYTHONIOENCODING: 'utf-8' };
        const child = spawn(venvPythonExecutable, ['-c', PY_INSTALL_SCRIPT, args], { env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
        for (const stream of [child.stdout, child.stderr]) {
            createInterface({ input: stream }).on('line', line => {
                if (line.startsWith('JOVIAN_PY_ERROR: ')) {
                    failure = line.slice('JOVIAN_PY_ERROR: '.length);
                } else if (line.startsWith('JOVIAN_PY_OFFLINE: ')) {
                    offline = line.slice('JOVIAN_PY_OFFLINE: '.length);
                } else if (line.startsWith('JOVIAN_PY_RESULT: ')) {
                    const parsed = JSON.parse(line.slice('JOVIAN_PY_RESULT: '.length)) as { previous?: string | null; version?: string | null; changed?: boolean; offline?: boolean };
                    result = { previousVersion: parsed.previous ?? undefined, version: parsed.version ?? undefined, changed: !!parsed.changed, offline: !!parsed.offline };
                } else if (line.startsWith('JOVIAN_PY: ')) {
                    options.onOutput?.(line.slice('JOVIAN_PY: '.length));
                } else if (line.trim()) {
                    otherOutput.push(line.trim());
                }
            });
        }

        const timeoutMs = options.timeoutMs ?? 30 * 60 * 1000;
        let timedOut = false;
        const timer = setTimeout(() => { timedOut = true; child.kill(); }, timeoutMs);
        const code = await new Promise<number | null>((resolve, reject) => {
            child.once('error', error => { clearTimeout(timer); reject(error); });
            child.once('close', exitCode => { clearTimeout(timer); resolve(exitCode); });
        });

        if (timedOut) {
            throw new Error(`Installing Python packages took longer than ${Math.round(timeoutMs / 60000)} minutes and was stopped.`);
        }
        if (offline) {
            const error = new Error(offline);
            error.name = PYTHON_PACKAGES_OFFLINE;
            throw error;
        }
        if (code !== 0 || !result) {
            throw new Error(failure ?? `Installing Python packages failed (python exited with code ${code})${otherOutput.length ? `: ${otherOutput.slice(-5).join(' | ')}` : ''}`);
        }
        return result;
    }
}
