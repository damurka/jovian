import { test } from 'node:test';
import * as assert from 'node:assert';
import * as fs from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
    buildInstallArgs,
    findAppRequirementFiles,
    readVenvHome,
    venvPython,
    venvSitePackages
} from '../../../dist/lib/session/python-packages.js';

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
