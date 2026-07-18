import { test } from 'node:test';
import * as assert from 'node:assert';
import { MiddlewareChain } from '../../../dist/lib/middleware/middleware-chain.js';

test('MiddlewareChain', async (t) => {
    await t.test('should process message through chain', async () => {
        const chain = new MiddlewareChain();
        
        chain.use({
            name: 'm1',
            process: (msg) => msg + '1'
        });
        
        chain.use({
            name: 'm2',
            process: async (msg) => msg + '2'
        });
        
        const result = await chain.process('test');
        assert.strictEqual(result, 'test12');
    });

    await t.test('should handle empty chain', async () => {
        const chain = new MiddlewareChain();
        const result = await chain.process('test');
        assert.strictEqual(result, 'test');
    });
});
