import { test } from 'node:test';
import * as assert from 'node:assert';
import { join } from 'node:path';
import { R_SETUP_SCRIPT, ensureRPackages, rscriptPath, type SetupDeps } from '../../../dist/lib/session/r-setup.js';

const logger = { debug: () => {}, info: () => {} };

interface Fake extends SetupDeps {
    runs: Array<{ rscript: string; args: string[]; env: NodeJS.ProcessEnv }>;
}

// `lines` are what the R script would print; `code` its exit status.
function fake(rHome: string, lines: string[] = [], code = 0, overrides: Partial<SetupDeps> = {}): Fake {
    const runs: Fake['runs'] = [];
    return {
        env: {},
        platform: 'linux',
        exists: (path) => path === join(rHome, 'bin', 'Rscript'),
        timeoutMs: 1000,
        run: async (rscript, _scriptFile, args, env, onLine) => {
            runs.push({ rscript, args, env });
            lines.forEach(onLine);
            return { code, timedOut: false };
        },
        runs,
        ...overrides
    };
}

test('rscriptPath', async (t) => {
    await t.test('finds bin/Rscript on POSIX', () => {
        assert.strictEqual(rscriptPath('/usr/lib/R', fake('/usr/lib/R')), join('/usr/lib/R', 'bin', 'Rscript'));
    });

    await t.test('finds Rscript.exe under bin or bin/x64 on Windows', () => {
        const win = (found: string) => ({ platform: 'win32', exists: (p: string) => p === found });
        const home = 'C:/R';
        assert.strictEqual(rscriptPath(home, win(join(home, 'bin', 'Rscript.exe'))), join(home, 'bin', 'Rscript.exe'));
        assert.strictEqual(rscriptPath(home, win(join(home, 'bin', 'x64', 'Rscript.exe'))), join(home, 'bin', 'x64', 'Rscript.exe'));
    });

    await t.test('is undefined when there is no Rscript', () => {
        assert.strictEqual(rscriptPath('/nowhere', fake('/usr/lib/R')), undefined);
    });
});

test('ensureRPackages does nothing when there is nothing to set up', async (t) => {
    await t.test('for a Python session', async () => {
        const deps = fake('/r/python');
        await ensureRPackages({ kernelType: 'python', rHome: '/r/python', heraSrcPath: '/hera' }, logger, deps);
        assert.strictEqual(deps.runs.length, 0);
    });

    await t.test('without a bundled hera (a source checkout)', async () => {
        const deps = fake('/r/nohera');
        await ensureRPackages({ rHome: '/r/nohera' }, logger, deps);
        assert.strictEqual(deps.runs.length, 0);
    });

    await t.test('without an R home', async () => {
        const deps = fake('/r/x');
        await ensureRPackages({ heraSrcPath: '/hera' }, logger, deps);
        assert.strictEqual(deps.runs.length, 0);
    });

    await t.test('when JOVIAN_SKIP_R_SETUP is set', async () => {
        const deps = fake('/r/skip', [], 0, { env: { JOVIAN_SKIP_R_SETUP: '1' } });
        await ensureRPackages({ rHome: '/r/skip', heraSrcPath: '/hera' }, logger, deps);
        assert.strictEqual(deps.runs.length, 0);
    });

    await t.test('when the R has no Rscript', async () => {
        const deps = fake('/r/other', [], 0, { exists: () => false });
        await ensureRPackages({ rHome: '/r/other', heraSrcPath: '/hera' }, logger, deps);
        assert.strictEqual(deps.runs.length, 0);
    });
});

test('ensureRPackages runs the setup script', async (t) => {
    await t.test('with the hera path, and rLibs as R_LIBS', async () => {
        const deps = fake('/r/args');
        await ensureRPackages({ rHome: '/r/args', heraSrcPath: '/pkg/hera', rLibs: '/my/lib' }, logger, deps);
        assert.strictEqual(deps.runs.length, 1);
        assert.deepStrictEqual(deps.runs[0]?.args, ['/pkg/hera']);
        assert.strictEqual(deps.runs[0]?.env.R_LIBS, '/my/lib');
    });

    await t.test('once for concurrent sessions, and not again after it succeeded', async () => {
        const deps = fake('/r/once');
        const options = { rHome: '/r/once', heraSrcPath: '/pkg/hera' };
        await Promise.all([ensureRPackages(options, logger, deps), ensureRPackages(options, logger, deps)]);
        await ensureRPackages(options, logger, deps);
        assert.strictEqual(deps.runs.length, 1);
    });

    await t.test('reports R\'s own reason when it fails, and tries again next time', async () => {
        const failing = fake('/r/fail', ['JOVIAN_R_SETUP: installing 2 R package(s)', 'JOVIAN_R_SETUP_ERROR: could not install these R packages: cli'], 1);
        await assert.rejects(
            ensureRPackages({ rHome: '/r/fail', heraSrcPath: '/pkg/hera' }, logger, failing),
            /Could not set up the R packages the kernel needs: could not install these R packages: cli/
        );
        const working = fake('/r/fail');
        await ensureRPackages({ rHome: '/r/fail', heraSrcPath: '/pkg/hera' }, logger, working);
        assert.strictEqual(working.runs.length, 1);
    });

    await t.test('says so when the script exits without explaining', async () => {
        await assert.rejects(
            ensureRPackages({ rHome: '/r/silent', heraSrcPath: '/pkg/hera' }, logger, fake('/r/silent', ['Error: something odd', ''], 3)),
            /Rscript exited with code 3: Error: something odd/
        );
    });

    await t.test('stops a run that takes too long', async () => {
        const slow = fake('/r/slow', [], 0, { run: async () => ({ code: null, timedOut: true }), timeoutMs: 120_000 });
        await assert.rejects(ensureRPackages({ rHome: '/r/slow', heraSrcPath: '/pkg/hera' }, logger, slow), /took longer than 2 minutes/);
    });
});

test('the R setup script', async (t) => {
    await t.test('needs nothing but base R: it does not use remotes', () => {
        assert.ok(!/remotes/.test(R_SETUP_SCRIPT.replace(/#.*$/gm, '')));
    });
});
