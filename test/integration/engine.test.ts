import { test } from 'node:test';
import * as assert from 'node:assert';
import { DatasuiteEngine } from '../../dist/lib/core/engine.js';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);

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
