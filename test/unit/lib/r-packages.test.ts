import { test } from 'node:test';
import * as assert from 'node:assert';
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
    ensureRPackageIn,
    folderSize,
    followRInstall,
    LIBRARY_LOCK_FILE,
    R_PACKAGES_OFFLINE,
    lockLibrary,
    type RPackageProgress
} from '../../../dist/lib/session/r-packages.js';

/** The progress an install's lines give: [line, fromScript] pairs, as ensureRPackage passes them. */
function follow(lines: Array<[string, boolean]>): RPackageProgress[] {
    const statuses: RPackageProgress[] = [];
    const onLine = followRInstall((status) => statuses.push(status));
    for (const [line, fromScript] of lines) onLine(line, fromScript);
    return statuses;
}

async function scratch(prefix: string): Promise<string> {
    return fs.promises.mkdtemp(join(tmpdir(), prefix));
}

test('followRInstall', async (t) => {
    await t.test('Windows binaries: checking, downloading, then each package as R unpacks it', () => {
        assert.deepStrictEqual(follow([
            ['Checking the repositories', true],
            ['Installing cli, glue, rlang', true],
            ['package ‘cli’ successfully unpacked and MD5 sums checked', false],
            ['package ‘glue’ successfully unpacked and MD5 sums checked', false],
            ['package ‘glue’ successfully unpacked and MD5 sums checked', false], // said twice: counted once
            ['package ‘rlang’ successfully unpacked and MD5 sums checked', false]
        ]), [
            { phase: 'checking' },
            { phase: 'downloading', done: 0, total: 3 },
            { phase: 'installing', done: 1, total: 3, current: 'cli' },
            { phase: 'installing', done: 2, total: 3, current: 'glue' },
            { phase: 'installing', done: 3, total: 3, current: 'rlang' }
        ]);
    });

    await t.test('source builds: the package being built, then done', () => {
        assert.deepStrictEqual(follow([
            ['Installing data.table, sf', true],
            ['* installing *source* package ‘data.table’ ...', false],
            ['gcc -I"/usr/share/R/include" -DNDEBUG -c assign.c -o assign.o', false],
            ['* DONE (data.table)', false],
            ['begin installing package ‘sf’', false],
            ['* DONE (sf)', false]
        ]), [
            { phase: 'downloading', done: 0, total: 2 },
            { phase: 'installing', done: 0, total: 2, current: 'data.table' },
            { phase: 'installing', done: 1, total: 2, current: 'data.table' },
            { phase: 'installing', done: 1, total: 2, current: 'sf' },
            { phase: 'installing', done: 2, total: 2, current: 'sf' }
        ]);
    });

    await t.test('retries keep the count; R’s other output and plain quotes', () => {
        assert.deepStrictEqual(follow([
            ['Installing a, b', true],
            ['package \'a\' successfully unpacked and MD5 sums checked', false],
            ['Some downloads did not finish: trying again', true],
            ['trying URL \'https://cloud.r-project.org/bin/windows/contrib/4.6/b_1.0.zip\'', false],
            ['Content type \'application/zip\' length 1024 bytes', false]
        ]), [
            { phase: 'downloading', done: 0, total: 2 },
            { phase: 'installing', done: 1, total: 2, current: 'a' },
            { phase: 'retrying', done: 1, total: 2 }
        ]);
    });

    await t.test('a line from R that looks like the script’s is not taken for it', () => {
        assert.deepStrictEqual(follow([['Installing packages into the library', false]]), []);
    });
});

test('folderSize', async (t) => {
    await t.test('the files that have arrived, not the folders beside them; a missing folder is nothing', async () => {
        const folder = await scratch('jovian-folder-size-');
        try {
            assert.strictEqual(await folderSize(folder), 0);
            await fs.promises.writeFile(join(folder, 'a_1.0.zip'), Buffer.alloc(1000));
            await fs.promises.writeFile(join(folder, 'b_2.0.zip'), Buffer.alloc(234));
            await fs.promises.mkdir(join(folder, 'unpacked'));
            await fs.promises.writeFile(join(folder, 'unpacked', 'c'), Buffer.alloc(99));
            assert.strictEqual(await folderSize(folder), 1234);
            assert.strictEqual(await folderSize(join(folder, 'gone')), 0);
        } finally {
            await fs.promises.rm(folder, { recursive: true, force: true });
        }
    });
});

test('lockLibrary', async (t) => {
    await t.test('one install at a time: the second waits until the first gives the library back', async () => {
        const library = await scratch('jovian-lock-');
        try {
            const release = await lockLibrary(library, 60_000, () => {});
            assert.ok(fs.existsSync(join(library, LIBRARY_LOCK_FILE)));
            let waited = false;
            let second = false;
            const next = lockLibrary(library, 60_000, () => { waited = true; }).then((releaseNext) => { second = true; return releaseNext; });
            await new Promise((resolve) => setTimeout(resolve, 300));
            assert.deepStrictEqual([waited, second], [true, false]);
            await release();
            await (await next)();
            assert.strictEqual(second, true);
            assert.ok(!fs.existsSync(join(library, LIBRARY_LOCK_FILE)));
        } finally {
            await fs.promises.rm(library, { recursive: true, force: true });
        }
    });

    await t.test('takes over the lock of a process that is gone', async () => {
        const library = await scratch('jovian-lock-');
        try {
            // a pid no process has (above the usual pid range)
            await fs.promises.writeFile(join(library, LIBRARY_LOCK_FILE), JSON.stringify({ pid: 2 ** 22 + 7, at: Date.now() }));
            let waited = false;
            const release = await lockLibrary(library, 60_000, () => { waited = true; });
            assert.strictEqual(waited, false);
            await release();
        } finally {
            await fs.promises.rm(library, { recursive: true, force: true });
        }
    });
});

test('ensureRPackageIn', async (t) => {
    /**
     * A packages session that answers .jv.pkg.ensure() as given: the plan (plan_only = TRUE) and the install, each a
     * list of lines it prints (in two chunks, a line split across them, as a kernel's stream can).
     */
    function fakeSession(plan: string[], install: string[]) {
        const listeners = new Set<(message: { msgType: string; content: unknown }) => void>();
        const calls: string[] = [];
        return {
            calls,
            on(_event: 'message', listener: (message: { msgType: string; content: unknown }) => void) { listeners.add(listener); },
            off(_event: 'message', listener: (message: { msgType: string; content: unknown }) => void) { listeners.delete(listener); },
            async execute(code: string) {
                calls.push(code.includes('plan_only = TRUE') ? 'plan' : 'install');
                const text = (code.includes('plan_only = TRUE') ? plan : install).map((line) => `${line}\n`).join('');
                const half = Math.floor(text.length / 2);
                for (const chunk of [text.slice(0, half), text.slice(half)]) {
                    for (const listener of listeners) listener({ msgType: 'stream', content: { name: 'stdout', text: chunk } });
                }
                return { success: true };
            }
        };
    }
    function fakeHost(session: ReturnType<typeof fakeSession>, using: string[][]) {
        const holds: string[] = [];
        return {
            holds,
            async packagesSession() { return session; },
            sessionsUsing() { return using.length > 1 ? using.shift()! : using[0] ?? []; },
            hold(library: string, until: Promise<unknown>) { holds.push(library); void until; }
        };
    }
    const library = await scratch('jovian-ensure-');
    t.after(() => fs.promises.rm(library, { recursive: true, force: true }));

    await t.test('nothing to install: one call, the plan says so', async () => {
        const session = fakeSession(['JOVIAN_PKG_RESULT: 1.8.1 1.8.1 - online'], []);
        const result = await ensureRPackageIn(fakeHost(session, [['app']]), { name: 'glue' }, { rHome: 'R', libraries: [library] });
        assert.deepStrictEqual(result, { previousVersion: '1.8.1', version: '1.8.1', installed: [], offline: false });
        assert.deepStrictEqual(session.calls, ['plan']);
    });

    await t.test('an update replacing packages waits for the sessions on the library, then holds new ones back', async () => {
        const session = fakeSession(['JOVIAN_PKG_PLAN: glue,cli glue'], ['JOVIAN_PKG: Installing glue, cli', 'package \'glue\' successfully unpacked and MD5 sums checked', 'JOVIAN_PKG_RESULT: 1.7.0 1.8.1 glue,cli online']);
        const host = fakeHost(session, [['app'], []]);
        const phases: string[] = [];
        const result = await ensureRPackageIn(host, { name: 'glue', update: true }, { rHome: 'R', libraries: [library], onProgress: (p) => phases.push(p.phase) });
        assert.deepStrictEqual(result.installed, ['glue', 'cli']);
        assert.deepStrictEqual(session.calls, ['plan', 'install']);
        assert.strictEqual(phases[0], 'waiting');
        assert.ok(phases.includes('installing'));
        assert.deepStrictEqual(host.holds, [library]);
    });

    await t.test('installing only what is missing does not wait for anyone', async () => {
        const session = fakeSession(['JOVIAN_PKG_PLAN: glue -'], ['JOVIAN_PKG_RESULT: NA 1.8.1 glue online']);
        const host = fakeHost(session, [['app']]);
        const phases: string[] = [];
        await ensureRPackageIn(host, { name: 'glue' }, { rHome: 'R', libraries: [library], onProgress: (p) => phases.push(p.phase) });
        assert.ok(!phases.includes('waiting'));
        assert.deepStrictEqual(host.holds, []);
    });

    await t.test('offline and failures reject as before', async () => {
        const offline = fakeSession(['JOVIAN_PKG_OFFLINE: Could not reach https://x.r-universe.dev.'], []);
        await assert.rejects(ensureRPackageIn(fakeHost(offline, [[]]), { name: 'glue' }, { rHome: 'R', libraries: [library] }), (e: Error) => e.name === R_PACKAGES_OFFLINE);
        const failed = fakeSession(['JOVIAN_PKG_PLAN: glue -'], ['JOVIAN_PKG_ERROR: Could not install glue.']);
        await assert.rejects(ensureRPackageIn(fakeHost(failed, [[]]), { name: 'glue' }, { rHome: 'R', libraries: [library] }), /Could not install glue/);
    });
});
