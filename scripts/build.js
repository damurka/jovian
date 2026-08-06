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
    
    console.log('📦 Step 1/2: Building native targets (addon + datasuite-r + datasuite-supervisor)...');
    const vcpkgRoot = process.env.VCPKG_ROOT || '';
    const toolchainFile = vcpkgRoot ? `${vcpkgRoot}/scripts/buildsystems/vcpkg.cmake` : '';

    const cmakeArgs = ['compile', '--out', 'dist/native'];
    if (toolchainFile) {
        cmakeArgs.push(`--CDCMAKE_TOOLCHAIN_FILE="${toolchainFile}"`);
    }

    // cmake-js configures the whole native/CMakeLists.txt project and runs a
    // plain `cmake --build` with no --target restriction, so this single
    // invocation already builds all three targets (datasuite_addon,
    // datasuite-r, datasuite-supervisor) -- they're independent options
    // (DATASUITE_BUILD_ADDON/KERNEL_EXE/SUPERVISOR, all default ON) in one
    // configure, not three separate builds.
    await run('npx', ['cmake-js', ...cmakeArgs]);
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
