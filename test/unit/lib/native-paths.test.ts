import { test } from 'node:test';
import * as assert from 'node:assert';
import { join, dirname } from 'node:path';
import * as release from '../../../scripts/release.mjs';
import {
    PACKAGE_NAME,
    SUPPORTED_PLATFORMS,
    bundledHeraSource,
    locateNativeDirectory,
    platformPackageName,
    type NativeLookup
} from '../../../dist/lib/session/native-paths.js';

function lookup(overrides: Partial<NativeLookup> & { present?: string[]; installed?: Record<string, string> } = {}): NativeLookup {
    const present = new Set(overrides.present ?? []);
    const installed = overrides.installed ?? {};
    return {
        env: {},
        platform: 'linux',
        arch: 'x64',
        sourceBuildDir: join('repo', 'dist', 'native', 'Release'),
        exists: (path) => present.has(path),
        resolvePackageJson: (name) => installed[name],
        ...overrides
    };
}

test('platformPackageName', async (t) => {
    await t.test('maps every supported platform to jovian-<os>-<cpu>', () => {
        for (const key of SUPPORTED_PLATFORMS) {
            const [os, cpu] = key.split('-');
            assert.strictEqual(platformPackageName(os, cpu), `@damurka/jovian-${key}`);
        }
    });

    await t.test('is undefined for anything else', () => {
        assert.strictEqual(platformPackageName('win32', 'arm64'), undefined);
        assert.strictEqual(platformPackageName('freebsd', 'x64'), undefined);
    });
});

test('locateNativeDirectory', async (t) => {
    await t.test('JOVIAN_NATIVE_DIR wins over an installed platform package', () => {
        const pkg = join('nm', '@damurka/jovian-linux-x64', 'package.json');
        const found = locateNativeDirectory(lookup({
            env: { JOVIAN_NATIVE_DIR: 'custom' },
            installed: { '@damurka/jovian-linux-x64': pkg },
            present: [join('custom', 'themisto'), join(dirname(pkg), 'bin', 'themisto')]
        }));
        assert.deepStrictEqual(found, { dir: 'custom', source: 'JOVIAN_NATIVE_DIR' });
    });

    await t.test('an override without a supervisor in it is an error, not a silent fallback', () => {
        assert.throws(
            () => locateNativeDirectory(lookup({ env: { JOVIAN_NATIVE_DIR: 'custom' } })),
            /JOVIAN_NATIVE_DIR is set to 'custom'/
        );
    });

    await t.test('uses the installed platform package when there is no override', () => {
        const pkg = join('nm', '@damurka/jovian-linux-x64', 'package.json');
        const bin = join(dirname(pkg), 'bin');
        const found = locateNativeDirectory(lookup({
            installed: { '@damurka/jovian-linux-x64': pkg },
            present: [join(bin, 'themisto')]
        }));
        assert.deepStrictEqual(found, { dir: bin, source: 'platform package' });
    });

    await t.test('looks for themisto.exe on Windows', () => {
        const pkg = join('nm', '@damurka/jovian-win32-x64', 'package.json');
        const bin = join(dirname(pkg), 'bin');
        const found = locateNativeDirectory(lookup({
            platform: 'win32',
            installed: { '@damurka/jovian-win32-x64': pkg },
            present: [join(bin, 'themisto.exe')]
        }));
        assert.strictEqual(found.dir, bin);
    });

    await t.test('falls back to a source build', () => {
        const dir = join('repo', 'dist', 'native', 'Release');
        const found = locateNativeDirectory(lookup({ present: [join(dir, 'themisto')] }));
        assert.deepStrictEqual(found, { dir, source: 'source build' });
    });

    await t.test('names the missing package on a supported platform', () => {
        assert.throws(() => locateNativeDirectory(lookup()), /@damurka\/jovian-linux-x64/);
    });

    await t.test('lists the supported platforms on an unsupported one', () => {
        assert.throws(
            () => locateNativeDirectory(lookup({ platform: 'freebsd' })),
            /no prebuilt kernels for freebsd-x64.*win32-x64/
        );
    });
});

test('bundledHeraSource', async (t) => {
    const installedDir = join('app', 'node_modules', '@damurka', 'jovian', 'dist', 'lib', 'session');
    const hera = join(installedDir, '..', '..', '..', 'packages', 'hera');

    await t.test('points at the copy shipped in the package when installed under node_modules', () => {
        const found = bundledHeraSource(installedDir, (p) => p === join(hera, 'DESCRIPTION'));
        assert.strictEqual(found, join('app', 'node_modules', '@damurka', 'jovian', 'packages', 'hera'));
    });

    await t.test('is undefined in a source checkout so development controls which hera loads', () => {
        const checkout = join('repo', 'dist', 'lib', 'session');
        assert.strictEqual(bundledHeraSource(checkout, () => true), undefined);
    });

    await t.test('is undefined if the package carries no hera', () => {
        assert.strictEqual(bundledHeraSource(installedDir, () => false), undefined);
    });
});

test('the runtime and the release script agree', async (t) => {
    await t.test('on the main package name', () => {
        assert.strictEqual(release.PACKAGE, PACKAGE_NAME);
    });

    await t.test('on the platforms, and on each platform package name', () => {
        assert.deepStrictEqual(Object.keys(release.TARGETS), [...SUPPORTED_PLATFORMS]);
        const manifest = release.mainManifest('1.2.3');
        for (const key of SUPPORTED_PLATFORMS) {
            const [os, cpu] = key.split('-');
            const name = platformPackageName(os, cpu);
            assert.strictEqual(release.platformManifest('1.2.3', key).name, name);
            assert.strictEqual(manifest.optionalDependencies[name!], '1.2.3');
        }
    });
});
