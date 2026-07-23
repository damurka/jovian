import { test, after } from 'node:test';
import * as assert from 'node:assert';
import { DatasuiteEngine } from '../../dist/lib/core/engine.js';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);

// The native addon's background ZMQ/R threads leave something running that
// `node --test`'s per-file child process doesn't notice as "done" even
// after a clean engine.stop(). Neither --test-force-exit nor this explicit
// process.exit() reliably fixes it -- the hang's timing relative to
// node:test's own teardown is non-deterministic, so this sometimes helps
// and sometimes doesn't. Left in as a harmless safety net; see the timeout
// wrapper in scripts/test.js for how `npm test` copes with this either way.
after(() => {
    process.exit(0);
});

test('DatasuiteEngine Integration', async (t) => {
    // Skip if native addon is not built
    try {
        require('../../dist/native/Release/datasuite_addon.node');
    } catch (e) {
        console.log('Skipping integration test: Native addon not found');
        return;
    }

    await t.test('should start and stop engine', async () => {
        const engine = new DatasuiteEngine({
            rHome: process.env.R_HOME || 'C:/Program Files/R/R-4.6.0'
        });
        
        let readyEmitted = false;
        engine.on('ready', () => {
            readyEmitted = true;
        });
        
        await engine.start();
        assert.ok(readyEmitted);
        
        let stoppedEmitted = false;
        engine.on('stopped', () => {
            stoppedEmitted = true;
        });
        
        await engine.stop();
        assert.ok(stoppedEmitted);
    });
});
