#!/usr/bin/env node
import { spawn } from 'child_process';
import { existsSync, mkdirSync } from 'fs';
import { join } from 'path';

const ROOT = process.cwd();
const DIST = join(ROOT, 'dist');

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

async function main() {
    console.log('🔨 Building datasuite-r...\n');
    
    if (!existsSync(DIST)) {
        mkdirSync(DIST, { recursive: true });
    }
    
    console.log('📦 Step 1/2: Building native targets (datasuite-r + datasuite-supervisor)...');
    const vcpkgRoot = process.env.VCPKG_ROOT || '';
    const toolchainFile = vcpkgRoot ? `${vcpkgRoot}/scripts/buildsystems/vcpkg.cmake` : '';

    // Plain CMake now that there's no Node addon target needing cmake-js's
    // Node-ABI-aware configure step (CMAKE_JS_INC/LIB, forced /MT runtime).
    // DATASUITE_BUILD_KERNEL_EXE/SUPERVISOR are independent options (both
    // default ON), so this single configure+build produces both targets.
    const configureArgs = ['-S', '.', '-B', 'dist/native'];
    if (toolchainFile) {
        configureArgs.push(`-DCMAKE_TOOLCHAIN_FILE="${toolchainFile}"`);
    }
    await run('cmake', configureArgs);
    await run('cmake', ['--build', 'dist/native', '--config', 'Release']);
    console.log('✅ Native targets built\n');
    
    console.log('📦 Step 2/2: Building TypeScript...');
    await run('npx', ['tsc', '--build']);
    console.log('✅ TypeScript compiled\n');
    
    console.log('🎉 Build complete!');
}

main().catch((err) => {
    console.error('❌ Build failed:', err.message);
    process.exit(1);
});
