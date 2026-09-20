// Minimal example: create one session, run some R code, print the result,
// stop the session. See examples/advanced/two-sessions.js for what this
// looks like with more than one session running concurrently.
//
// Run from the repo root after `npm run build`:
//   node examples/basic/simple-execute.js
//
// rHome/rPath/rLibs below default to a plain `R_HOME` env var if you have
// one set (e.g. from `R RHOME` on your PATH); otherwise set them directly
// to your own R installation's paths.
import { SessionManager } from '../../dist/lib/index.js';

const manager = new SessionManager();

const session = await manager.createSession({
    rHome: process.env.R_HOME,
    rPath: process.env.R_PATH,
    rLibs: process.env.R_LIBS
});

session.on('stdout', (text) => process.stdout.write(text));

const result = await session.execute('print("Hello from R!"); 1 + 1');
console.log('success:', result.success);

// manager.stopAll() (not session.stop()) also kills the shared supervisor
// process it spawned -- without that, this script would hang indefinitely:
// the supervisor's stdout/stderr listeners (SessionManager's own
// SupervisorClient) keep Node's event loop alive until it's killed.
await manager.stopAll();
