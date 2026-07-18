#!/usr/bin/env node
import { rmSync, existsSync } from 'fs';
import { join } from 'path';

const ROOT = process.cwd();
const dirs = [
    join(ROOT, 'dist'),
    join(ROOT, 'build'),
    join(ROOT, 'out'),
    join(ROOT, '.cmake-js')
];

console.log('🧹 Cleaning build artifacts...\n');

for (const dir of dirs) {
    if (existsSync(dir)) {
        console.log(`  Removing ${dir}`);
        rmSync(dir, { recursive: true, force: true });
    }
}

console.log('\n✅ Clean complete!');
