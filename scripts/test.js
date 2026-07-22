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
    console.log('🧪 Running tests...\n');
    
    console.log('📦 Running C++ tests...');
    try {
        // Build the tests first
        await run('npx', ['cmake-js', 'compile', '--out', 'dist/native', '--CDCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake"', '--CDDATASUITE_BUILD_TESTS=ON']);
        // Run the tests using CTest
        await run('ctest', ['--test-dir', 'dist/native', '--output-on-failure']);
        console.log('✅ C++ tests complete\n');
    } catch (err) {
        console.error('❌ C++ tests failed:', err.message);
        throw err;
    }

    console.log('📦 Running unit tests...');
    await run('node', ['--test', '--experimental-test-coverage', 'test/unit/lib/*.test.ts']);
    console.log('✅ Unit tests complete\n');
    
    console.log('📦 Running integration tests...');
    await run('node', ['--test', 'test/integration/*.test.ts']);
    console.log('✅ Integration tests complete\n');
    
    console.log('🎉 All tests complete!');
}

main().catch((err) => {
    console.error('❌ Tests failed:', err.message);
    process.exit(1);
});
