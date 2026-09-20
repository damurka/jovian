// TypeScript version of simple-execute.js -- same behavior, using the
// package's own published types (EngineOptions, ExecutionResult) instead
// of relying on plain JS inference. Run from the repo root after
// `npm run build`:
//   npx tsx examples/basic/simple-execute.ts
import { SessionManager } from '../../dist/lib/index.js';
import type { EngineOptions, ExecutionResult } from '../../dist/lib/types/index.js';

const manager = new SessionManager();

const options: EngineOptions = {
    rHome: process.env.R_HOME,
    rPath: process.env.R_PATH,
    rLibs: process.env.R_LIBS
};

const session = await manager.createSession(options);

session.on('stdout', (text: string) => process.stdout.write(text));

const result: ExecutionResult = await session.execute('print("Hello from R!"); 1 + 1');
console.log('success:', result.success);

await manager.stopAll();
