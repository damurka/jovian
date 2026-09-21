#!/usr/bin/env node
// Installs the packed tarballs into a scratch project the way a user would
// (main package + this machine's platform package, from tarballs, not from
// the source tree) and starts a real kernel from them.
//
//   node scripts/release-smoke.mjs --dir dist/release/tarballs [--python]
//
// rHome / pythonHome are deliberately not passed: the library has to find R and
// Python itself, as it does for a user.
// R_LIBS is pointed at an empty scratch library, so the bundled hera is
// installed from the package on the kernel's first start instead of using
// (or touching) whatever hera is already in your R library.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { currentTarget, PACKAGE } from './release.mjs';

const args = process.argv.slice(2);
const option = (name) => (args.includes(`--${name}`) ? args[args.indexOf(`--${name}`) + 1] : undefined);
const dir = resolve(option('dir') ?? 'dist/release/tarballs');
const withPython = args.includes('--python');

const tarballPrefix = PACKAGE.replace('@', '').replace('/', '-');
const target = currentTarget();
const files = readdirSync(dir);
const platformTarball = files.find((f) => f.startsWith(`${tarballPrefix}-${target}-`) && f.endsWith('.tgz'));
const mainTarball = files.find((f) => f.startsWith(`${tarballPrefix}-`) && f.endsWith('.tgz') && !/-(win32|linux|darwin)-/.test(f));
if (!platformTarball || !mainTarball) {
    console.error(`smoke: need both ${tarballPrefix}-<version>.tgz and ${tarballPrefix}-${target}-<version>.tgz in ${dir} (found: ${files.join(', ') || 'nothing'})`);
    process.exit(1);
}

const project = mkdtempSync(join(tmpdir(), 'jovian-smoke-'));
const rlib = join(project, 'rlib');
mkdirSync(rlib);
console.log(`smoke: scratch project ${project}`);

function run(command, commandArgs, options = {}) {
    const result = spawnSync(command, commandArgs, { cwd: project, stdio: 'inherit', shell: true, ...options });
    if (result.status !== 0) {
        console.error(`smoke: '${command} ${commandArgs.join(' ')}' failed (${result.status ?? result.signal})`);
        process.exit(1);
    }
}

writeFileSync(join(project, 'package.json'), JSON.stringify({ name: 'smoke', private: true, type: 'module' }));
// The optional platform dependencies of the main package do not exist on the
// registry yet; the platform tarball for this machine is installed directly.
run('npm', ['install', '--omit=optional', '--no-audit', '--no-fund', '--loglevel=error',
    `"${join(dir, platformTarball)}"`, `"${join(dir, mainTarball)}"`]);

writeFileSync(join(project, 'smoke.mjs'), `
import { SessionManager } from '${PACKAGE}';

const manager = new SessionManager();
let failed = false;
const check = (label, ok, detail) => {
    console.log((ok ? 'ok   ' : 'FAIL ') + label + (ok ? '' : ': ' + detail));
    failed ||= !ok;
};

try {
    const r = await manager.createSession({ kernelType: 'r' });
    r.on('error', () => {});
    const sum = await r.execute('1 + 1');
    check('R: 1 + 1', sum.success && JSON.stringify(sum.output).includes('[1] 2'), JSON.stringify(sum));
    const hera = await r.execute('as.character(packageVersion("hera"))');
    check('R: hera loaded', hera.success, JSON.stringify(hera));
    const completion = await r.complete('me', 2);
    check('R: complete()', Array.isArray(completion?.matches) && completion.matches.length > 0, JSON.stringify(completion));

    if (${withPython}) {
        const py = await manager.createSession({ kernelType: 'python' });
        py.on('error', () => {});
        const total = await py.execute('sum(range(1, 11))');
        check('Python: sum(range(1, 11))', total.success && JSON.stringify(total.output).includes('"55"'), JSON.stringify(total));
    }
} catch (error) {
    check('session', false, String(error?.stack ?? error));
} finally {
    await manager.stopAll();
}
process.exit(failed ? 1 : 0);
`);
const smoke = spawnSync(process.execPath, ['smoke.mjs'], {
    cwd: project,
    stdio: 'inherit',
    env: { ...process.env, R_LIBS: rlib, JOVIAN_NATIVE_DIR: '' }
});
// hera must have been installed from the package into the scratch library. Without
// this a hera already in your own R library would let a broken bundle pass.
let exitCode = smoke.status ?? 1;
if (!existsSync(join(rlib, 'hera'))) {
    console.error('FAIL hera was not installed from the package into the scratch R library');
    exitCode = 1;
}
if (!process.env.JOVIAN_KEEP_SMOKE) rmSync(project, { recursive: true, force: true });
process.exit(exitCode);
