import { chmodSync, existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

/** The npm scope the packages are published under (also set in scripts/release.mjs; a test keeps them in step). */
export const PACKAGE_SCOPE = '@damurka';
export const PACKAGE_NAME = `${PACKAGE_SCOPE}/jovian`;

/**
 * The prebuilt kernels (themisto, elara, carpo) ship in one small package per
 * platform -- `@scope/jovian-<os>-<cpu>` -- that npm installs alongside
 * the main package only where it matches (each declares `os`/`cpu`). These are
 * the platforms that get a package.
 */
export const SUPPORTED_PLATFORMS = ['win32-x64', 'linux-x64', 'darwin-arm64'] as const;

export function platformPackageName(platform: string = process.platform, arch: string = process.arch): string | undefined {
    const key = `${platform}-${arch}`;
    return (SUPPORTED_PLATFORMS as readonly string[]).includes(key) ? `${PACKAGE_NAME}-${key}` : undefined;
}

export interface NativeLocation {
    /** Directory holding themisto, elara and carpo. */
    dir: string;
    source: 'JOVIAN_NATIVE_DIR' | 'platform package' | 'source build';
}

export interface NativeLookup {
    env: Record<string, string | undefined>;
    platform: string;
    arch: string;
    exists: (path: string) => boolean;
    /** Absolute path of a package's package.json, or undefined if it is not installed. */
    resolvePackageJson: (packageName: string) => string | undefined;
    /** dist/native/Release of a source checkout. */
    sourceBuildDir: string;
}

const moduleDir = dirname(fileURLToPath(import.meta.url));

function defaultLookup(): NativeLookup {
    const require = createRequire(import.meta.url);
    return {
        env: process.env,
        platform: process.platform,
        arch: process.arch,
        exists: existsSync,
        resolvePackageJson: (name) => {
            try {
                return require.resolve(`${name}/package.json`);
            } catch {
                return undefined;
            }
        },
        // dist/lib/session -> dist/native/Release
        sourceBuildDir: join(moduleDir, '../../native/Release')
    };
}

/**
 * Where the kernels are, in order of precedence:
 *   1. JOVIAN_NATIVE_DIR -- an explicit directory (a copy of the binaries, an
 *      app that bundles them elsewhere);
 *   2. the installed `@scope/jovian-<os>-<cpu>` package for this machine;
 *   3. a source checkout's dist/native/Release (development).
 */
export function locateNativeDirectory(lookup: NativeLookup = defaultLookup()): NativeLocation {
    const exe = lookup.platform === 'win32' ? 'themisto.exe' : 'themisto';
    const has = (dir: string) => lookup.exists(join(dir, exe));

    const override = lookup.env.JOVIAN_NATIVE_DIR;
    if (override) {
        if (!has(override)) {
            throw new Error(`jovian: JOVIAN_NATIVE_DIR is set to '${override}' but there is no ${exe} in it.`);
        }
        return { dir: override, source: 'JOVIAN_NATIVE_DIR' };
    }

    const packageName = platformPackageName(lookup.platform, lookup.arch);
    if (packageName) {
        const packageJson = lookup.resolvePackageJson(packageName);
        if (packageJson) {
            const dir = join(dirname(packageJson), 'bin');
            if (has(dir)) {
                return { dir, source: 'platform package' };
            }
        }
    }

    if (has(lookup.sourceBuildDir)) {
        return { dir: lookup.sourceBuildDir, source: 'source build' };
    }

    if (!packageName) {
        throw new Error(
            `jovian: there are no prebuilt kernels for ${lookup.platform}-${lookup.arch} ` +
            `(supported: ${SUPPORTED_PLATFORMS.join(', ')}). Build them from source and point ` +
            `JOVIAN_NATIVE_DIR at the output directory.`
        );
    }
    throw new Error(
        `jovian: the kernel binaries were not found. Expected the '${packageName}' package ` +
        `(installed automatically as an optional dependency -- reinstall without --omit=optional / ` +
        `--no-optional), or a source build at ${lookup.sourceBuildDir}, or JOVIAN_NATIVE_DIR.`
    );
}

/**
 * npm does not reliably keep the executable bit on files in a tarball, so make
 * sure the kernels can be run (POSIX only; a no-op on Windows).
 */
export function ensureExecutable(dir: string, platform: string = process.platform): void {
    if (platform === 'win32') return;
    for (const name of ['themisto', 'elara', 'carpo']) {
        try {
            chmodSync(join(dir, name), 0o755);
        } catch {
            // Absent (carpo is optional) or not ours to change: running it will say so.
        }
    }
}

/**
 * The copy of the 'hera' R package that ships inside the npm package, used to
 * install/refresh it in R on a session's first start when the caller gave no
 * heraSrcPath. Only when running from an installed package (under
 * node_modules): a source checkout leaves hera alone so development and CI
 * control which one is loaded.
 */
export function bundledHeraSource(
    baseDir: string = moduleDir,
    exists: (path: string) => boolean = existsSync
): string | undefined {
    if (!baseDir.split(sep).includes('node_modules')) return undefined;
    // dist/lib/session -> the package root
    const candidate = join(baseDir, '../../../packages/hera');
    return exists(join(candidate, 'DESCRIPTION')) ? candidate : undefined;
}
