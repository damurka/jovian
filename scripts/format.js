#!/usr/bin/env node
import { spawn } from 'child_process';
import { existsSync } from 'fs';
import { glob } from 'glob';

const ROOT = process.cwd();
const isDryRun = process.argv.includes('--dry-run');

async function run(cmd, args) {
    return new Promise((resolve, reject) => {
        const proc = spawn(cmd, args, {
            stdio: 'inherit',
            shell: true,
            cwd: ROOT
        });

        proc.on('close', (code) => {
            if (code === 0) resolve();
            else reject(new Error(`Command failed with code ${code}`));
        });
    });
}

async function formatCpp() {
    const files = await glob('native/{include,src,test}/**/*.{cpp,hpp,h}', { cwd: ROOT });
    if (files.length === 0) {
        console.log('  (no C++ files found)');
        return;
    }

    // No .clang-format config is checked into this repo, so clang-format
    // falls back to its built-in default (LLVM) style rather than
    // whatever house style was previously in use.
    if (!existsSync('.clang-format')) {
        console.log('  ⚠️  No .clang-format found -- using clang-format\'s built-in default style.');
    }

    const args = isDryRun
        ? ['--dry-run', '--Werror', ...files]
        : ['-i', ...files];

    await run('npx', ['clang-format', ...args]);
}

async function formatTs() {
    const hasFlatConfig = existsSync('eslint.config.js') || existsSync('eslint.config.mjs') || existsSync('eslint.config.cjs');
    const hasLegacyConfig = existsSync('.eslintrc') || existsSync('.eslintrc.json') || existsSync('.eslintrc.js') || existsSync('.eslintrc.cjs');

    if (!hasFlatConfig && !hasLegacyConfig) {
        console.log('  ⚠️  No ESLint config found (ESLint 9 needs eslint.config.js) -- skipping TypeScript formatting.');
        return;
    }

    const args = isDryRun
        ? ['eslint', 'lib/**/*.ts', 'test/**/*.ts']
        : ['eslint', 'lib/**/*.ts', 'test/**/*.ts', '--fix'];

    await run('npx', args);
}

async function main() {
    console.log(isDryRun ? '🔍 Checking formatting...\n' : '🎨 Formatting...\n');

    console.log('📦 C++ (clang-format)...');
    await formatCpp();
    console.log('✅ C++ done\n');

    console.log('📦 TypeScript (eslint)...');
    await formatTs();
    console.log('✅ TypeScript done\n');

    console.log(isDryRun ? '🎉 Formatting check complete!' : '🎉 Formatting complete!');
}

main().catch((err) => {
    console.error(`❌ Format${isDryRun ? ' check' : ''} failed:`, err.message);
    process.exit(1);
});
