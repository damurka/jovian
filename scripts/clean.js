#!/usr/bin/env node
import { rmSync, existsSync } from 'fs';
import { join } from 'path';

const ROOT = process.cwd();
const dirs = [
    // The one real build output root: dist/lib (tsc), dist/native (the
    // main CMake build), dist/native-test (scripts/test.js's separate
    // JOVIAN_BUILD_TESTS=ON tree), dist/ide/<preset> and
    // dist/ide-install/<preset> (CMakePresets.json, for VS/CMake Tools).
    join(ROOT, 'dist'),
    // Legacy/pre-migration paths: nothing currently writes to these
    // (CMakePresets.json used to put its output in out/build/<preset>; a
    // removed cmake-js/N-API addon setup used to write to build/ and
    // .cmake-js/), kept here only to clean up leftovers on a checkout that
    // predates that migration.
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
