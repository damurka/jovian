import { test } from 'node:test';
import * as assert from 'node:assert';
import { execSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '../../..');

// tsc --init turns on exactOptionalPropertyTypes; the published option types
// must accept `process.env.X` (string | undefined) there, as the README does.
test('the README example type-checks for a strict consumer', () => {
    try {
        execSync('npx tsc -p test/types/tsconfig.json', { cwd: root, encoding: 'utf8', stdio: 'pipe' });
    } catch (error) {
        const failure = error as { stdout?: string; stderr?: string };
        assert.fail(`tsc failed:\n${failure.stdout ?? ''}${failure.stderr ?? ''}`);
    }
});
