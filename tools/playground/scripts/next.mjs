// Thin launcher around the `next` CLI: picks the port from PLAYGROUND_PORT
// (default 4173, what the old playground used) in a way that works in every
// shell, and binds to loopback only -- this tool executes arbitrary code in
// real kernels, so it must not be reachable from other machines.
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const nextBin = require.resolve('next/dist/bin/next');

const [command = 'dev', ...rest] = process.argv.slice(2);
const port = process.env.PLAYGROUND_PORT || process.env.PORT || '4173';
const args = [nextBin, command];
if (command === 'dev' || command === 'start') {
    args.push('-p', port, '-H', '127.0.0.1');
}
args.push(...rest);

if (command === 'dev' || command === 'start') {
    console.log(`\nJovian playground: http://127.0.0.1:${port}\n`);
}

const child = spawn(process.execPath, args, { stdio: 'inherit', env: process.env });
for (const signal of ['SIGINT', 'SIGTERM']) {
    process.on(signal, () => child.kill(signal));
}
child.on('exit', (code, signal) => process.exit(code ?? (signal ? 1 : 0)));
