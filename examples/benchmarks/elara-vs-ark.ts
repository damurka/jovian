// Benchmark: Jovian's R kernel (Elara) against Positron's Ark, both run by the
// same supervisor (themisto) on the same R installation.
//
// Measures, for each kernel, in a fresh session:
//   1. create session -> first result of `1+1` (ms)
//   2. trivial cell `1+1`, median of 30 runs (ms)
//   3. 200k lines: `for (i in 1:200000) cat(i, "\n")` wall time, all lines arrived?
//   4. 10 MB write: `cat(strrep("x", 1e7))` wall time, all bytes arrived?
//   5. trickle latency: R stamps each `cat` with its own clock inside
//      `for (i in 1:10) { ...; Sys.sleep(0.1) }`; mean/max delay until it reaches Node
//   6. flood: 25k `cat` + `message` pairs: wall time, stream messages received, all lines present
// and prints a markdown table (measure | Elara | Ark). "Wall time" runs from
// execute() until both the execute() promise has resolved and the last expected
// line has arrived.
//
// Run from the repo root after `npm run build:lib` (Node >= 22.18 runs .ts directly):
//
//   node examples/benchmarks/elara-vs-ark.ts
//
// Environment (all optional):
//   JOVIAN_NATIVE_DIR  directory holding themisto/elara (e.g. an installed
//                      @damurka/jovian-win32-x64/bin); otherwise the usual lookup
//   R_HOME             R installation; otherwise discovered (discoverRHome)
//   ARK_PATH           ark executable; otherwise Positron's copy (discoverArkPath).
//                      If none is found only Elara is measured.
//   BENCH_HERA_SRC     hera sources Elara installs/loads (default: packages/hera)
//   BENCH_R_LIBS       R library hera is installed into (default: a new temporary
//                      directory, deleted at the end -- your own libraries are
//                      never written to)
//   BENCH_KERNELS      comma-separated subset of "elara,ark"
//
// The busy helper (a second R process for completions) is turned off so only the
// kernels themselves are compared. Each kernel gets one untimed warm-up session
// first (starts the supervisor, installs hera into the temporary library, warms
// the disk cache), so the timed create is a warm start for both.

import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { delimiter, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { SessionManager } from '../../dist/lib/index.js';
import { discoverArkPath, discoverRHome } from '../../dist/lib/session/runtimes.js';
import { locateNativeDirectory } from '../../dist/lib/session/native-paths.js';
import { rscriptPath } from '../../dist/lib/session/r-setup.js';
import type { EngineOptions, JupyterMessage, Session } from '../../dist/lib/index.js';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const LONG = 10 * 60 * 1000; // generous execute() timeout (Ark is slow on floods)

type Kernel = 'elara' | 'ark';
type Row = Record<string, string>;

const MEASURES = [
    'create -> first result (ms)',
    '`1+1` median of 30 (ms)',
    '200k lines: wall (s)',
    '200k lines: all arrived',
    '10 MB write: wall (ms)',
    '10 MB write: all bytes',
    'trickle: mean delay (ms)',
    'trickle: max delay (ms)',
    '25k cat+message: wall (s)',
    '25k cat+message: stream msgs',
    '25k cat+message: all lines'
] as const;

// ---------------------------------------------------------------------------
// Output capture: every stream message, with its arrival time.

interface Chunk { name: string; text: string; at: number }

class Capture {
    chunks: Chunk[] = [];
    constructor(session: Session) {
        session.on('message', (m: JupyterMessage) => {
            if (m.msgType === 'stream') {
                const c = m.content as { name: string; text: string };
                this.chunks.push({ name: c.name, text: c.text ?? '', at: performance.now() });
            }
        });
    }
    reset(): void { this.chunks = []; }
    text(name: 'stdout' | 'stderr'): string {
        return this.chunks.filter((c) => c.name === name).map((c) => c.text).join('');
    }
    lastAt(): number { return this.chunks.length ? this.chunks[this.chunks.length - 1].at : 0; }
}

async function waitFor(done: () => boolean, ms: number): Promise<boolean> {
    const end = Date.now() + ms;
    while (!done()) {
        if (Date.now() > end) return false;
        await new Promise((r) => setTimeout(r, 10));
    }
    return true;
}

function lineNumbers(text: string): Set<number> {
    const seen = new Set<number>();
    for (const line of text.split('\n')) {
        const t = line.trim();
        if (t) seen.add(Number(t));
    }
    return seen;
}

function allPresent(seen: Set<number>, n: number): boolean {
    for (let i = 1; i <= n; i++) if (!seen.has(i)) return false;
    return true;
}

function missingCount(seen: Set<number>, n: number): number {
    let missing = 0;
    for (let i = 1; i <= n; i++) if (!seen.has(i)) missing++;
    return missing;
}

async function run(session: Session, code: string): Promise<void> {
    const result = await session.execute(code, { timeout: LONG });
    if (!result.success) {
        throw new Error(result.error?.message ?? `execution failed: ${code.slice(0, 60)}`);
    }
}

// ---------------------------------------------------------------------------

async function benchmark(manager: SessionManager, kernel: Kernel, base: EngineOptions): Promise<Row> {
    const row: Row = {};
    const options: EngineOptions = kernel === 'ark' ? { ...base, kernelType: 'ark' } : { ...base, kernelType: 'r' };
    const step = async (names: string[], body: () => Promise<void>) => {
        try {
            await body();
        } catch (error) {
            const reason = `failed: ${(error as Error).message.split('\n')[0].slice(0, 80)}`;
            for (const n of names) row[n] ??= reason;
            console.error(`  [${kernel}] ${names[0]}: ${reason}`);
        }
    };

    // Untimed warm-up session.
    console.error(`[${kernel}] warm-up session...`);
    const warm = await manager.createSession(options);
    await run(warm, '1+1');
    if (kernel === 'elara') {
        const cap = new Capture(warm);
        await run(warm, 'if (requireNamespace("hera", quietly = TRUE)) cat("HERA", as.character(packageVersion("hera")), find.package("hera"), "\\n") else cat("HERA none\\n")');
        const line = cap.text('stdout').trim();
        row.hera = line.startsWith('HERA ') ? line.slice(5) : line || 'unknown';
        console.error(`[elara] hera: ${row.hera}`);
    }
    await warm.stop();

    // 1. create -> first result
    const t0 = performance.now();
    const session = await manager.createSession(options);
    await run(session, '1+1');
    row[MEASURES[0]] = (performance.now() - t0).toFixed(0);
    const cap = new Capture(session);
    console.error(`[${kernel}] create -> first result: ${row[MEASURES[0]]} ms`);

    // 2. trivial cell
    await step([MEASURES[1]], async () => {
        for (let i = 0; i < 3; i++) await run(session, '1+1');
        const times: number[] = [];
        for (let i = 0; i < 30; i++) {
            const s = performance.now();
            await run(session, '1+1');
            times.push(performance.now() - s);
        }
        times.sort((a, b) => a - b);
        row[MEASURES[1]] = ((times[14] + times[15]) / 2).toFixed(1);
        console.error(`[${kernel}] 1+1 median: ${row[MEASURES[1]]} ms`);
    });

    // 3. 200k lines
    await step([MEASURES[2], MEASURES[3]], async () => {
        const N = 200000;
        cap.reset();
        const s = performance.now();
        await run(session, `for (i in 1:${N}) cat(i, "\\n")`);
        const resolved = performance.now();
        await waitFor(() => cap.text('stdout').includes(`${N} \n`), 10000);
        const seen = lineNumbers(cap.text('stdout'));
        row[MEASURES[2]] = ((Math.max(resolved, cap.lastAt()) - s) / 1000).toFixed(2);
        const ok = allPresent(seen, N);
        row[MEASURES[3]] = ok ? `yes (${cap.chunks.length} msgs)` : `NO: ${missingCount(seen, N)} missing`;
        console.error(`[${kernel}] 200k lines: ${row[MEASURES[2]]} s, ${row[MEASURES[3]]}`);
    });

    // 4. 10 MB write
    await step([MEASURES[4], MEASURES[5]], async () => {
        const N = 1e7;
        cap.reset();
        const count = () => cap.chunks.reduce((n, c) => n + (c.name === 'stdout' ? c.text.split('x').length - 1 : 0), 0);
        const s = performance.now();
        await run(session, 'cat(strrep("x", 1e7))');
        const resolved = performance.now();
        await waitFor(() => count() >= N, 10000);
        row[MEASURES[4]] = (Math.max(resolved, cap.lastAt()) - s).toFixed(0);
        const got = count();
        row[MEASURES[5]] = got === N ? `yes (${cap.chunks.length} msgs)` : `NO: ${got} of ${N}`;
        console.error(`[${kernel}] 10 MB: ${row[MEASURES[4]]} ms, ${row[MEASURES[5]]}`);
    });

    // 5. trickle: R stamps each line with its own clock (ms since the epoch),
    // compared with when it arrives here (same machine, same clock).
    await step([MEASURES[6], MEASURES[7]], async () => {
        cap.reset();
        const arrivals = new Map<number, number>();
        const onMessage = (m: JupyterMessage) => {
            if (m.msgType !== 'stream') return;
            const now = Date.now();
            const text = (m.content as { text: string }).text ?? '';
            for (const hit of text.matchAll(/T(\d+)=(\d+(?:\.\d+)?)/g)) {
                arrivals.set(Number(hit[1]), now - Number(hit[2]));
            }
        };
        session.on('message', onMessage);
        try {
            await run(session, 'for (i in 1:10) { cat(sprintf("T%d=%.3f\\n", i, as.numeric(Sys.time()) * 1000)); Sys.sleep(0.1) }');
            await waitFor(() => arrivals.size >= 10, 5000);
        } finally {
            session.off('message', onMessage);
        }
        const delays = [...arrivals.values()];
        if (delays.length === 0) throw new Error('no stamped lines arrived');
        row[MEASURES[6]] = (delays.reduce((a, b) => a + b, 0) / delays.length).toFixed(1);
        row[MEASURES[7]] = Math.max(...delays).toFixed(1) + (delays.length < 10 ? ` (${delays.length}/10 lines)` : '');
        console.error(`[${kernel}] trickle: mean ${row[MEASURES[6]]} ms, max ${row[MEASURES[7]]} ms`);
    });

    // 6. flood of cat + message pairs
    await step([MEASURES[8], MEASURES[9], MEASURES[10]], async () => {
        const N = 25000;
        cap.reset();
        const s = performance.now();
        let timedOut = '';
        try {
            await run(session, `for (i in 1:${N}) { cat(i, "\\n"); message(i) }`);
        } catch (error) {
            timedOut = ` (${(error as Error).message.split('\n')[0].slice(0, 60)})`;
        }
        const resolved = performance.now();
        // cat() lines are "i " (trailing space), message() lines "i". Checked
        // across both streams: Ark sends message() output as stdout, Elara as stderr.
        const split = () => {
            const cats = new Set<number>();
            const messages = new Set<number>();
            for (const line of (cap.text('stdout') + '\n' + cap.text('stderr')).split(/\r?\n/)) {
                if (/^\d+ $/.test(line)) cats.add(Number(line));
                else if (/^\d+$/.test(line)) messages.add(Number(line));
            }
            return { cats, messages };
        };
        await waitFor(() => { const { cats, messages } = split(); return cats.has(N) && messages.has(N); }, 10000);
        row[MEASURES[8]] = ((Math.max(resolved, cap.lastAt()) - s) / 1000).toFixed(2) + timedOut;
        const { cats, messages } = split();
        const streams = [...new Set(cap.chunks.map((c) => c.name))].join('+');
        row[MEASURES[9]] = `${cap.chunks.length} (${streams})`;
        const ok = allPresent(cats, N) && allPresent(messages, N);
        row[MEASURES[10]] = ok ? 'yes' : `NO: ${missingCount(cats, N)} cat + ${missingCount(messages, N)} message missing`;
        console.error(`[${kernel}] flood: ${row[MEASURES[8]]} s, ${row[MEASURES[9]]} msgs, ${row[MEASURES[10]]}`);
    });

    await session.stop();
    return row;
}

// ---------------------------------------------------------------------------

const native = locateNativeDirectory();
const rHome = process.env.R_HOME || (await discoverRHome());
const arkPath = await discoverArkPath();
const heraSrcPath = resolve(process.env.BENCH_HERA_SRC ?? join(repoRoot, 'packages', 'hera'));
// Elara sets R_LIBS, R_LIBS_USER and R_LIBS_SITE all to rLibs, so a lone
// temporary directory would hide the packages hera needs (cli, evaluate, ...).
// Put the temporary directory first -- hera is installed into the first
// writable library -- followed by this R's own non-system libraries, read only.
function userLibraries(home: string): string[] {
    const rscript = rscriptPath(home);
    if (!rscript) return [];
    try {
        const out = execFileSync(rscript, ['-e', 'cat(setdiff(.libPaths(), .Library), sep = "\\n")'], { encoding: 'utf8' });
        return out.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
    } catch {
        return [];
    }
}
const tempLib = process.env.BENCH_R_LIBS ? undefined : mkdtempSync(join(tmpdir(), 'jovian-bench-rlib-'));
const rLibs = process.env.BENCH_R_LIBS ?? [tempLib!, ...(rHome ? userLibraries(rHome) : [])].join(delimiter);
// Install hera into the temporary library before anything starts. Without
// this, a hera already in your own library counts as installed and would be
// the one loaded; installed here it comes first in .libPaths() and wins.
if (tempLib && rHome) {
    const rscript = rscriptPath(rHome);
    const rExe = rscript && join(dirname(rscript), process.platform === 'win32' ? 'R.exe' : 'R');
    console.error(`installing hera from ${heraSrcPath} into ${tempLib} ...`);
    try {
        execFileSync(rExe!, ['CMD', 'INSTALL', '--no-multiarch', `--library=${tempLib}`, heraSrcPath], {
            env: { ...process.env, R_LIBS: rLibs }, stdio: ['ignore', 'ignore', 'pipe'], encoding: 'utf8'
        });
    } catch (error) {
        console.error(`hera could not be installed into the temporary library: ${(error as { stderr?: string }).stderr?.slice(-400) ?? error}`);
    }
}
const wanted =(process.env.BENCH_KERNELS ?? 'elara,ark').split(',').map((k) => k.trim()) as Kernel[];

console.error(`kernels:   ${native.dir} (${native.source})`);
console.error(`R:         ${rHome ?? 'not found'}`);
console.error(`ark:       ${arkPath ?? 'not found -- measuring Elara only'}`);
console.error(`hera src:  ${heraSrcPath}`);
console.error(`R library: ${rLibs}${tempLib ? ' (first entry temporary, deleted at the end)' : ''}`);
if (!rHome) {
    console.error('No R installation found (set R_HOME).');
    process.exit(1);
}

const kernels = wanted.filter((k) => k === 'elara' || (k === 'ark' && arkPath));
const manager = new SessionManager({ busyHelper: false });
const rows: Partial<Record<Kernel, Row>> = {};
try {
    for (const kernel of kernels) {
        const base: EngineOptions = { rHome, rLibs, ...(kernel === 'elara' ? { heraSrcPath } : { arkPath }) };
        try {
            rows[kernel] = await benchmark(manager, kernel, base);
        } catch (error) {
            console.error(`[${kernel}] failed: ${(error as Error).message}`);
            rows[kernel] = Object.fromEntries(MEASURES.map((m) => [m, 'failed']));
        }
    }
} finally {
    await manager.stopAll();
    if (tempLib) rmSync(tempLib, { recursive: true, force: true });
}

const cell = (k: Kernel, m: string) => (rows[k] ? rows[k]![m] ?? '-' : arkPath || k === 'elara' ? 'skipped' : 'ark not found');
console.log('');
console.log(`R: ${rHome}  |  kernels: ${native.dir}  |  hera (Elara): ${rows.elara?.hera ?? '-'}`);
console.log('');
console.log('| measure | Elara | Ark |');
console.log('|---|---|---|');
for (const m of MEASURES) console.log(`| ${m} | ${cell('elara', m)} | ${cell('ark', m)} |`);
