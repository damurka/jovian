#!/usr/bin/env node
import { spawn, execSync } from 'child_process';

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

// The native addon's background ZMQ/R threads leave something running that
// `node --test`'s per-file child process never notices as "done", even
// after a clean engine.stop() -- neither --test-force-exit nor an explicit
// process.exit() from an after() hook in the test file itself reliably
// fixes it, and the hang's timing relative to node:test's own TAP output
// is non-deterministic (sometimes the "ok"/summary lines print first,
// sometimes the hang lands before them), so output can't be trusted as a
// pass/fail signal once the timeout fires. Root cause not fully identified
// (needs OS-level process inspection beyond what's practical from here).
// Until then: hard-kill after a generous timeout so this can't hang forever,
// but always treat a timeout as a failure -- if the real assertions had
// already printed "ok" above, that's visible in the output even though the
// overall step is reported as failed.
async function runNodeTest(args, timeoutMs = 20000) {
    return new Promise((resolve, reject) => {
        const proc = spawn('node', args, {
            cwd: ROOT,
            shell: true,
            stdio: 'inherit'
        });

        let settled = false;
        const finish = (fn, value) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            fn(value);
        };

        const timer = setTimeout(() => {
            // proc.kill() alone doesn't reliably take down node --test's own
            // child process on Windows; ask the OS to kill the whole tree.
            if (process.platform === 'win32') {
                try { execSync(`taskkill /pid ${proc.pid} /t /f`); } catch { /* already gone */ }
            } else {
                try { process.kill(-proc.pid, 'SIGKILL'); } catch { /* already gone */ }
            }
            console.error(
                `\n⚠️  node --test didn't exit within ${timeoutMs}ms and was force-killed. ` +
                `Check the output above -- if you see "ok" / a passing summary, the tests ` +
                `themselves likely passed and this is the known post-test hang, not a real failure.`
            );
            finish(reject, new Error('node --test timed out (see warning above)'));
        }, timeoutMs);

        proc.on('close', (code) => {
            if (code === 0) finish(resolve, undefined);
            else finish(reject, new Error(`Command failed with code ${code}`));
        });
    });
}

async function main() {
    console.log('🧪 Running tests...\n');

    console.log('📦 Running C++ tests...');
    try {
        // Built in a separate directory from dist/native (the one
        // build:native/compile use) so DATASUITE_BUILD_TESTS=ON doesn't
        // stick around in the main build's CMakeCache and silently make
        // every future `npm run build:native` also compile the test targets.
        const vcpkgRoot = process.env.VCPKG_ROOT || '';
        const toolchainFile = vcpkgRoot ? `${vcpkgRoot}/scripts/buildsystems/vcpkg.cmake` : '';

        const cmakeArgs = ['compile', '--out', 'dist/native-test', '--CDDATASUITE_BUILD_TESTS=ON'];
        if (toolchainFile) {
            cmakeArgs.push(`--CDCMAKE_TOOLCHAIN_FILE="${toolchainFile}"`);
        }

        await run('npx', ['cmake-js', ...cmakeArgs]);
        // Run the tests using CTest
        await run('ctest', ['--test-dir', 'dist/native-test', '--output-on-failure']);
        console.log('✅ C++ tests complete\n');
    } catch (err) {
        console.error('❌ C++ tests failed:', err.message);
        throw err;
    }

    console.log('📦 Running unit tests...');
    await runNodeTest(['--test', '--test-force-exit', '--experimental-test-coverage', 'test/unit/lib/*.test.ts']);
    console.log('✅ Unit tests complete\n');

    console.log('📦 Running integration tests...');
    await runNodeTest(['--test', '--test-force-exit', 'test/integration/*.test.ts']);
    console.log('✅ Integration tests complete\n');

    console.log('🎉 All tests complete!');
}

main().catch((err) => {
    console.error('❌ Tests failed:', err.message);
    process.exit(1);
});
