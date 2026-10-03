import { test } from 'node:test';
import * as assert from 'node:assert';
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
    ensureRPackage,
    folderSize,
    followRInstall,
    LIBRARY_LOCK_FILE,
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

test('ensureRPackage', async (t) => {
    await t.test('rejects at once when the R home has no Rscript', async () => {
        await assert.rejects(ensureRPackage({ name: 'glue' }, { rHome: join(tmpdir(), 'no-such-r') }), /no Rscript/);
    });
});
