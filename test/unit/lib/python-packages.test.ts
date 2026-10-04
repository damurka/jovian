import { test } from 'node:test';
import * as assert from 'node:assert';
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import {
    buildInstallArgs,
    ensurePythonEnvironmentIn,
    ensurePythonPackagesIn,
    findAppRequirementFiles,
    PYTHON_PACKAGES_OFFLINE,
    readVenvHome,
    venvPython,
    venvSitePackages,
    type PythonInstallRun,
    type PythonPackageResult
} from '../../../dist/lib/session/python-packages.js';
import { LIBRARY_LOCK_FILE, PACKAGES_IN_USE } from '../../../dist/lib/session/r-packages.js';

test('venvPython and venvSitePackages', () => {
    assert.strictEqual(venvPython('C:\\env', 'win32'), join('C:\\env', 'Scripts', 'python.exe'));
    assert.strictEqual(venvPython('/env', 'linux'), join('/env', 'bin', 'python'));
    assert.strictEqual(venvSitePackages('C:\\env', '3.12.10', 'win32'), join('C:\\env', 'Lib', 'site-packages'));
    assert.strictEqual(venvSitePackages('/env', '3.12.10', 'linux'), join('/env', 'lib', 'python3.12', 'site-packages'));
});

test('readVenvHome', () => {
    assert.strictEqual(readVenvHome('home = C:\\Python312\ninclude-system-site-packages = false\n'), 'C:\\Python312');
    assert.strictEqual(readVenvHome('version = 3.12.1\n'), undefined);
});

test('buildInstallArgs', async (t) => {
    await t.test('the caller’s requirements, the package with its minimum, the index, update', () => {
        assert.deepStrictEqual(
            buildInstallArgs({ requirements: ['shiny>=1.0'], name: 'mypkg', minVersion: '1.2', index: 'https://me.example/simple', update: true }, {}),
            { requirements: ['shiny>=1.0'], requirementsFile: undefined, pyproject: undefined, package: { name: 'mypkg', version: '1.2' }, index: 'https://me.example/simple', update: true }
        );
    });

    await t.test('requirements.txt wins over pyproject.toml', () => {
        const args = buildInstallArgs({}, { requirementsFile: '/app/requirements.txt', pyproject: '/app/pyproject.toml' }) as Record<string, unknown>;
        assert.deepStrictEqual([args.requirementsFile, args.pyproject, args.requirements], ['/app/requirements.txt', undefined, []]);
    });
});

test('findAppRequirementFiles', async () => {
    const app = await fs.promises.mkdtemp(join(tmpdir(), 'jovian-py-app-'));
    try {
        assert.deepStrictEqual(findAppRequirementFiles(app), { requirementsFile: undefined, pyproject: undefined });
        await fs.promises.writeFile(join(app, 'pyproject.toml'), '[project]\n');
        assert.deepStrictEqual(findAppRequirementFiles(app), { requirementsFile: undefined, pyproject: join(app, 'pyproject.toml') });
        assert.deepStrictEqual(findAppRequirementFiles(undefined), {});
    } finally {
        await fs.promises.rm(app, { recursive: true, force: true });
    }
});

test('ensurePythonPackagesIn', async (t) => {
    /** A runner answering the plan (planOnly) and the install as given, recording which it was asked for. */
    function fakeRunner(plan: Partial<PythonInstallRun>, install: Partial<PythonInstallRun>) {
        const calls: string[] = [];
        const run = async (_python: string, args: string, _timeoutMs: number, onOutput: (line: string) => void): Promise<PythonInstallRun> => {
            const planOnly = !!(JSON.parse(args) as { planOnly?: boolean }).planOnly;
            calls.push(planOnly ? 'plan' : 'install');
            onOutput(planOnly ? 'Checking' : 'Installing');
            return { other: [], code: 0, ...(planOnly ? plan : install) };
        };
        return { calls, run };
    }
    /** A host whose steps go into `log` (a runner's own calls, to have them in the order they happen). */
    function fakeHost(using: string[][], log: string[] = []) {
        return {
            log,
            async sessionsUsing() { log.push('ask'); return using.length > 1 ? using.shift()! : using[0] ?? []; },
            hold() { log.push('hold'); return () => { log.push('release'); }; }
        };
    }
    const done: PythonPackageResult = { previousVersion: '1.16.0', version: '1.17.0', changed: true, offline: false };
    const venv = await fs.promises.mkdtemp(join(tmpdir(), 'jovian-venv-'));
    t.after(() => fs.promises.rm(venv, { recursive: true, force: true }));

    await t.test('nothing to install: one run, the plan says so', async () => {
        const runner = fakeRunner({ result: { version: '1.17.0', previousVersion: '1.17.0', changed: false, offline: false } }, {});
        const result = await ensurePythonPackagesIn(fakeHost([['notebook']], runner.calls), venv, { name: 'six' }, {}, runner.run);
        assert.strictEqual(result.changed, false);
        assert.deepStrictEqual(runner.calls, ['hold', 'ask', 'release', 'plan']);
        assert.ok(!fs.existsSync(join(venv, LIBRARY_LOCK_FILE)), 'the environment is given back');
    });

    await t.test('replacing packages waits for the sessions on the environment, then holds new ones back', async () => {
        const runner = fakeRunner({ plan: { install: ['six'], replace: ['six'] } }, { result: done });
        const host = fakeHost([['notebook'], ['notebook'], []], runner.calls);
        const lines: string[] = [];
        const result = await ensurePythonPackagesIn(host, venv, { name: 'six', update: true }, { onOutput: (line) => lines.push(line) }, runner.run);
        assert.deepStrictEqual(result, done);
        // New sessions are held off the environment before who uses it is asked, each time; let in again while it
        // plans and waits for the notebook; held for the install.
        assert.deepStrictEqual(runner.calls, ['hold', 'ask', 'release', 'plan', 'hold', 'ask', 'release', 'hold', 'ask', 'install', 'release']);
        assert.ok(lines.some((line) => /^Waiting for 1 session using the environment to end before replacing six$/.test(line)), JSON.stringify(lines));
    });

    await t.test('installing only what is missing does not wait for anyone', async () => {
        const runner = fakeRunner({ plan: { install: ['plotly'], replace: [] } }, { result: done });
        const host = fakeHost([['notebook']], runner.calls);
        const lines: string[] = [];
        await ensurePythonPackagesIn(host, venv, { requirements: ['plotly'] }, { onOutput: (line) => lines.push(line) }, runner.run);
        assert.ok(!lines.some((line) => line.startsWith('Waiting')));
        // nothing is replaced: not held for the install
        assert.deepStrictEqual(runner.calls, ['hold', 'ask', 'release', 'plan', 'install']);
    });

    await t.test('no session on the environment: no plan (no one to wait for), new sessions held for the whole install', async () => {
        const runner = fakeRunner({}, { result: done });
        const host = fakeHost([[]], runner.calls);
        await ensurePythonPackagesIn(host, venv, { requirements: ['plotly'] }, {}, runner.run);
        assert.deepStrictEqual(runner.calls, ['hold', 'ask', 'install', 'release']);
    });

    await t.test('whenInUse proceed: no plan, straight to the install', async () => {
        const runner = fakeRunner({}, { result: done });
        await ensurePythonPackagesIn(fakeHost([['notebook']]), venv, { name: 'six' }, { whenInUse: 'proceed' }, runner.run);
        assert.deepStrictEqual(runner.calls, ['install']);
    });

    await t.test('whenInUse defer: replacing what a session uses installs nothing and says which sessions', async () => {
        const runner = fakeRunner({ plan: { install: ['six'], replace: ['six'] } }, { result: done });
        await assert.rejects(ensurePythonPackagesIn(fakeHost([['notebook']], runner.calls), venv, { name: 'six', update: true }, { whenInUse: 'defer' }, runner.run),
            (e: Error & { sessions?: string[] }) => e.name === PACKAGES_IN_USE && e.sessions?.[0] === 'notebook');
        // nothing installed, and nothing left held
        assert.deepStrictEqual(runner.calls, ['hold', 'ask', 'release', 'plan', 'hold', 'ask', 'release']);
    });

    await t.test('offline and failures reject as before', async () => {
        const offlineLine = 'Could not reach the Python package index to install six.';
        // the plan (a session on the environment) and the install (none) end the same way
        const offline = fakeRunner({ offline: offlineLine, code: 1 }, { offline: offlineLine, code: 1 });
        await assert.rejects(ensurePythonPackagesIn(fakeHost([['notebook']]), venv, { name: 'six' }, {}, offline.run), (e: Error) => e.name === PYTHON_PACKAGES_OFFLINE);
        await assert.rejects(ensurePythonPackagesIn(fakeHost([[]]), venv, { name: 'six' }, {}, offline.run), (e: Error) => e.name === PYTHON_PACKAGES_OFFLINE);
        const failed = fakeRunner({ plan: { install: ['six'], replace: [] } }, { failure: 'Could not install six.', code: 1 });
        await assert.rejects(ensurePythonPackagesIn(fakeHost([[]]), venv, { name: 'six' }, {}, failed.run), /Could not install six/);
    });
});

test('ensurePythonEnvironmentIn', async (t) => {
    await t.test('an environment made from this Python is left alone: no waiting, nothing held', async () => {
        const venv = await fs.promises.mkdtemp(join(tmpdir(), 'jovian-venv-'));
        try {
            const python = join(venv, 'base', 'python.exe');
            await fs.promises.writeFile(join(venv, 'pyvenv.cfg'), `home = ${dirname(python)}\n`);
            await fs.promises.mkdir(dirname(venvPython(venv)), { recursive: true });
            await fs.promises.writeFile(venvPython(venv), '');
            const holds: string[] = [];
            await ensurePythonEnvironmentIn({ sessionsUsing: async () => ['notebook'], hold: (dir) => { holds.push(dir); return () => { }; } }, python, venv);
            assert.deepStrictEqual(holds, []);
        } finally {
            await fs.promises.rm(venv, { recursive: true, force: true });
        }
    });
});
