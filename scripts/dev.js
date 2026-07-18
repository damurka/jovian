#!/usr/bin/env node
import { spawn } from 'child_process';

const ROOT = process.cwd();

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
    console.log('🚀 Starting development mode...\n');
    
    console.log('📦 Building C++ addon...');
    const vcpkgRoot = process.env.VCPKG_ROOT || '';
    const toolchainFile = vcpkgRoot ? `${vcpkgRoot}/scripts/buildsystems/vcpkg.cmake` : '';
    
    const cmakeArgs = ['compile', '--out', 'dist/native'];
    if (toolchainFile) {
        cmakeArgs.push(`--CDCMAKE_TOOLCHAIN_FILE="${toolchainFile}"`);
    }
    
    await run('npx', ['cmake-js', ...cmakeArgs]);
    console.log('✅ Native addon built\n');
    
    console.log('👀 Watching TypeScript files...');
    await run('npx', ['tsc', '--watch']);
}

main().catch((err) => {
    console.error('❌ Dev mode failed:', err.message);
    process.exit(1);
});
