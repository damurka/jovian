// Times what hera does on every message, output and cell: a flood of message() calls, many display_data() calls,
// one large HTML output, and many small cells (each also reports the session's state for the busy-time helper).
//
//   node examples/benchmarks/hera-overhead.ts
//
// Uses the built supervisor and kernel (dist/). Prints one line per measure, the median of RUNS runs.

import { SessionManager } from '../../dist/lib/index.js';
import { discoverRHome } from '../../dist/lib/session/runtimes.js';

const RUNS = 5;

const manager = new SessionManager();
const session = await manager.createSession({ rHome: await discoverRHome() });
session.on('error', () => { /* measured cells don't fail */ });

async function time(label: string, run: () => Promise<unknown>): Promise<void> {
    const times: number[] = [];
    for (let i = 0; i < RUNS; i++) {
        const start = performance.now();
        await run();
        times.push(performance.now() - start);
    }
    times.sort((a, b) => a - b);
    console.log(`${label.padEnd(44)} ${times[Math.floor(RUNS / 2)].toFixed(0).padStart(6)} ms`);
}

try {
    await session.execute('1');
    await time('25 000 message() calls', () => session.execute('for (i in 1:25000) message(i)', { timeout: 120_000 }));
    await time('2 000 display_data() calls', () => session.execute('for (i in 1:2000) hera::display_data(list("text/plain" = "x"))', { timeout: 120_000 }));
    await time('one 8 MB HTML output', () => session.execute('hera::display_data(list("text/html" = strrep("<b>x</b>", 1e6)))', { timeout: 120_000 }));
    await time('a data frame value (head(mtcars))', () => session.execute('head(mtcars)'));
    await time('200 cells of `1` (with the state query)', async () => {
        for (let i = 0; i < 200; i++) await session.execute('1');
    });
} finally {
    await manager.stopAll();
}
