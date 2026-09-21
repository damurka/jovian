#!/usr/bin/env node
// Stages the npm packages under dist/release/. Nothing here publishes; see
// docs/releasing.md and .github/workflows/release.yml.
//
//   node scripts/release.mjs main     --version 0.1.0
//   node scripts/release.mjs platform --version 0.1.0 [--target win32-x64] [--from dist/native/Release]
//
// `main` stages @scope/jovian (the TypeScript library + the hera R package);
// `platform` stages @scope/jovian-<os>-<cpu> (the prebuilt kernels for one
// platform, which the main package pulls in through optionalDependencies).

import { chmodSync, cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(ROOT, 'dist', 'release');

// The npm scope: keep in step with PACKAGE_SCOPE in lib/session/native-paths.ts
// (test/unit/lib/native-paths.test.ts fails if they differ).
export const SCOPE = '@damurka';
export const PACKAGE = `${SCOPE}/jovian`;
export const TARGETS = {
    'win32-x64': { os: 'win32', cpu: 'x64' },
    'linux-x64': { os: 'linux', cpu: 'x64' }
    // linux-arm64, darwin-arm64 and darwin-x64 are not published yet; see docs/releasing.md.
};

const DIR_NAME = PACKAGE.split('/').pop();
const REPOSITORY = 'https://github.com/damurka/jovian';
const KERNELS = ['themisto', 'elara', 'carpo'];
// Everything else the Windows build puts next to the executables that the
// kernels need at run time (vcpkg's dynamic libraries). Test binaries,
// gtest, PDBs and import libraries are not among them.
const RUNTIME_LIB = /\.(dll|so(\.\d+)*|dylib)$/;
const NOT_SHIPPED = /^(gtest|gmock)|_test\.|dummy_process_helper|plain_diag/;

export function currentTarget() {
    return `${process.platform}-${process.arch}`;
}

export function validateVersion(version) {
    if (!version || !/^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$/.test(version)) {
        throw new Error(`--version must be a semver like 0.1.0 (got '${version ?? ''}')`);
    }
    return version;
}

function common(version) {
    return {
        version,
        license: 'MIT',
        author: 'David Kariuki',
        repository: { type: 'git', url: `git+${REPOSITORY}.git` },
        homepage: `${REPOSITORY}#readme`,
        bugs: { url: `${REPOSITORY}/issues` },
        publishConfig: { access: 'public', provenance: true }
    };
}

export function mainManifest(version) {
    const optionalDependencies = {};
    for (const target of Object.keys(TARGETS)) optionalDependencies[`${PACKAGE}-${target}`] = version;
    return {
        name: PACKAGE,
        ...common(version),
        description: 'Supervised R and Python Jupyter kernels for Node.js and Electron, backed by native C++ processes',
        keywords: ['r', 'python', 'jupyter', 'kernel', 'repl', 'data-science', 'electron'],
        type: 'module',
        main: './lib/index.js',
        types: './lib/index.d.ts',
        exports: {
            '.': { types: './lib/index.d.ts', import: './lib/index.js' },
            './types': { types: './lib/types/index.d.ts', import: './lib/types/index.js' },
            './package.json': './package.json'
        },
        engines: { node: '>=22.4.0' },
        files: ['lib', 'packages/hera', 'docs', 'README.md', 'LICENSE'],
        optionalDependencies
    };
}

export function platformManifest(version, target) {
    const { os, cpu } = TARGETS[target];
    return {
        name: `${PACKAGE}-${target}`,
        ...common(version),
        description: `Prebuilt Jovian kernels (themisto, elara, carpo) for ${os} ${cpu}. Install ${PACKAGE}, not this.`,
        os: [os],
        cpu: [cpu],
        files: ['bin', 'README.md', 'LICENSE']
    };
}

function copyFiltered(from, to, keep) {
    cpSync(from, to, { recursive: true, filter: (src) => keep(src.replace(/\\/g, '/')) });
}

function fresh(dir) {
    rmSync(dir, { recursive: true, force: true });
    mkdirSync(dir, { recursive: true });
}

function writeJson(path, value) {
    writeFileSync(path, JSON.stringify(value, null, 2) + '\n');
}

export function stageMain(version, out = OUT) {
    const dist = join(ROOT, 'dist', 'lib');
    if (!existsSync(join(dist, 'index.js'))) {
        throw new Error('dist/lib is missing: run `npm run build:lib` first.');
    }
    const dir = join(out, DIR_NAME);
    fresh(dir);

    // The compiled library, without source maps / incremental build state.
    copyFiltered(dist, join(dir, 'lib'), (p) => !/\.(map|tsbuildinfo)$/.test(p));

    // hera is installed into R from this copy on a session's first start.
    const hera = join(ROOT, 'packages', 'hera');
    for (const entry of ['DESCRIPTION', 'NAMESPACE', 'LICENSE', 'LICENSE.md', 'NEWS.md', 'R', 'man']) {
        if (existsSync(join(hera, entry))) {
            cpSync(join(hera, entry), join(dir, 'packages', 'hera', entry), { recursive: true });
        }
    }

    // Lets elara reinstall hera after an upgrade (npm resets file mtimes, so
    // the kernel's mtime-based staleness check cannot see one).
    const description = join(dir, 'packages', 'hera', 'DESCRIPTION');
    if (existsSync(description)) {
        const text = readFileSync(description, 'utf8').replace(/^Config\/jovian\/release:.*\r?\n?/m, '');
        writeFileSync(description, `${text.replace(/\s*$/, '')}\nConfig/jovian/release: ${version}\n`);
    }

    cpSync(join(ROOT, 'docs'), join(dir, 'docs'), { recursive: true });
    cpSync(join(ROOT, 'README.md'), join(dir, 'README.md'));
    cpSync(join(ROOT, 'LICENSE'), join(dir, 'LICENSE'));
    writeJson(join(dir, 'package.json'), mainManifest(version));
    return dir;
}

export function stagePlatform(version, target, from, out = OUT) {
    if (!TARGETS[target]) {
        throw new Error(`unknown --target '${target}' (one of ${Object.keys(TARGETS).join(', ')})`);
    }
    const win = TARGETS[target].os === 'win32';
    const source = resolve(from);
    const exe = (name) => (win ? `${name}.exe` : name);
    for (const required of ['themisto', 'elara']) {
        if (!existsSync(join(source, exe(required)))) {
            throw new Error(`${join(source, exe(required))} not found: build the native targets first.`);
        }
    }

    const dir = join(out, `${DIR_NAME}-${target}`);
    fresh(dir);
    const bin = join(dir, 'bin');
    mkdirSync(bin, { recursive: true });

    const shipped = [];
    for (const name of readdirSync(source)) {
        const path = join(source, name);
        if (!statSync(path).isFile() || NOT_SHIPPED.test(name)) continue;
        const isKernel = KERNELS.some((k) => name === exe(k));
        if (!isKernel && !(win && RUNTIME_LIB.test(name))) continue;
        cpSync(path, join(bin, name));
        if (!win) chmodSync(join(bin, name), 0o755);
        shipped.push(name);
    }

    writeFileSync(join(dir, 'README.md'),
        `# ${PACKAGE}-${target}\n\nPrebuilt kernels (themisto, elara${shipped.includes(exe('carpo')) ? ', carpo' : ''}) for ${target}.\n` +
        `This is an implementation detail of [${PACKAGE}](https://www.npmjs.com/package/${PACKAGE}): install that package, not this one.\n`);
    cpSync(join(ROOT, 'LICENSE'), join(dir, 'LICENSE'));
    writeJson(join(dir, 'package.json'), platformManifest(version, target));
    return { dir, shipped };
}

function parseArgs(argv) {
    const args = { _: [] };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i].startsWith('--')) args[argv[i].slice(2)] = argv[++i];
        else args._.push(argv[i]);
    }
    return args;
}

function main() {
    const args = parseArgs(process.argv.slice(2));
    const command = args._[0];
    try {
        if (command === 'main') {
            console.log(`staged ${stageMain(validateVersion(args.version))}`);
        } else if (command === 'platform') {
            const target = args.target ?? currentTarget();
            const from = args.from ?? join(ROOT, 'dist', 'native', 'Release');
            const { dir, shipped } = stagePlatform(validateVersion(args.version), target, from);
            console.log(`staged ${dir}\n  bin/: ${shipped.join(', ')}`);
        } else {
            console.error('usage: release.mjs main --version X | platform --version X [--target T] [--from DIR]');
            process.exit(2);
        }
    } catch (error) {
        console.error(`release: ${error.message}`);
        process.exit(1);
    }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
