import { test } from 'node:test';
import * as assert from 'node:assert';
import { ExecutionQueue } from '../../../dist/lib/execution/execution-queue.js';

test('ExecutionQueue', async (t) => {
    await t.test('should queue and execute sequentially', async () => {
        const executed: string[] = [];
        const mockAddon = {
            execute: (code: string) => {
                executed.push(code);
            }
        };
        
        const queue = new ExecutionQueue(mockAddon);
        
        // Start multiple executions
        const p1 = queue.execute('code1');
        const p2 = queue.execute('code2');
        
        assert.strictEqual(queue.size, 1); // One executing, one queued
        
        // In a real scenario, we'd wait for the addon to finish and trigger the next
        // For this simple test, we just verify they were queued
    });

    await t.test('should reject when queue is full', async () => {
        const mockAddon = { execute: () => {} };
        const queue = new ExecutionQueue(mockAddon, 1); // Max size 1
        
        // First one starts executing
        queue.execute('code1');
        
        // Second one fills the queue
        queue.execute('code2');
        
        // Third one should be rejected
        await assert.rejects(
            queue.execute('code3'),
            /Execution queue is full/
        );
    });

    await t.test('should clear queue', async () => {
        const mockAddon = { execute: () => {} };
        const queue = new ExecutionQueue(mockAddon);
        
        queue.execute('code1');
        const p2 = queue.execute('code2');
        
        queue.clear();
        
        assert.strictEqual(queue.size, 0);
        await assert.rejects(p2, /Queue cleared/);
    });
});
