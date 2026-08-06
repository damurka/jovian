#!/usr/bin/env node
// Runs the native CTest suite under OpenCppCoverage and prints a per-file
// summary table, mirroring the shape of node --experimental-test-coverage's
// report so C++ and TS coverage read the same way in `npm test` output.
import { spawn } from 'child_process';
import { existsSync, readFileSync, unlinkSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

const OPENCPPCOVERAGE_FALLBACK = 'C:\\Program Files\\OpenCppCoverage\\OpenCppCoverage.exe';

function findOpenCppCoverage() {
    if (process.env.OPENCPPCOVERAGE_PATH) return process.env.OPENCPPCOVERAGE_PATH;
    if (existsSync(OPENCPPCOVERAGE_FALLBACK)) return OPENCPPCOVERAGE_FALLBACK;
    return 'OpenCppCoverage'; // hope it's on PATH
}

// Merges same-source-file <class> blocks across OpenCppCoverage's
// per-module <package> elements (one package per test .exe run under
// --cover_children) -- a shared file like authentication.cpp gets hit by
// several test binaries, and each contributes its own partial view of
// which lines it happened to exercise.
function parseCobertura(xml) {
    const files = new Map(); // filename -> Map<lineNumber, hits>
    const classRe = /<class\s+name="[^"]*"\s+filename="([^"]*)"[^>]*>([\s\S]*?)<\/class>/g;
    const lineRe = /<line number="(\d+)" hits="(\d+)"\s*\/>/g;

    let classMatch;
    while ((classMatch = classRe.exec(xml)) !== null) {
        const filename = classMatch[1].replace(/\\/g, '/');
        const body = classMatch[2];
        let lines = files.get(filename);
        if (!lines) {
            lines = new Map();
            files.set(filename, lines);
        }
        let lineMatch;
        while ((lineMatch = lineRe.exec(body)) !== null) {
            const num = Number(lineMatch[1]);
            const hits = Number(lineMatch[2]);
            lines.set(num, (lines.get(num) || 0) + hits);
        }
    }
    return files;
}

function compressRanges(nums) {
    if (nums.length === 0) return '';
    const sorted = [...nums].sort((a, b) => a - b);
    const ranges = [];
    let start = sorted[0];
    let prev = sorted[0];
    for (let i = 1; i < sorted.length; i++) {
        if (sorted[i] === prev + 1) {
            prev = sorted[i];
            continue;
        }
        ranges.push(start === prev ? `${start}` : `${start}-${prev}`);
        start = prev = sorted[i];
    }
    ranges.push(start === prev ? `${start}` : `${start}-${prev}`);
    return ranges.join(',');
}

// Shortens the PDB's absolute path down to the native/src-relative form
// used everywhere else in this repo (test comments, --sources filter).
function relativeToNativeSrc(filename) {
    const idx = filename.toLowerCase().indexOf('native/src/');
    return idx === -1 ? filename : filename.slice(idx);
}

function printReport(files) {
    const rows = [...files.entries()]
        .map(([filename, lines]) => {
            const total = lines.size;
            const covered = [...lines.values()].filter((h) => h > 0).length;
            const uncovered = [...lines.entries()]
                .filter(([, h]) => h === 0)
                .map(([n]) => n);
            return {
                file: relativeToNativeSrc(filename),
                pct: total === 0 ? 100 : (covered / total) * 100,
                total,
                covered,
                uncoveredLines: compressRanges(uncovered)
            };
        })
        .sort((a, b) => a.file.localeCompare(b.file));

    const totalLines = rows.reduce((sum, r) => sum + r.total, 0);
    const totalCovered = rows.reduce((sum, r) => sum + r.covered, 0);
    const overallPct = totalLines === 0 ? 100 : (totalCovered / totalLines) * 100;

    const fileWidth = Math.max(4, ...rows.map((r) => r.file.length));
    const sep = '-'.repeat(fileWidth + 40);

    console.log('\nℹ start of C++ coverage report');
    console.log(sep);
    console.log(`file${' '.repeat(fileWidth - 4)} | line % | uncovered lines`);
    console.log(sep);
    for (const r of rows) {
        console.log(`${r.file.padEnd(fileWidth)} | ${r.pct.toFixed(2).padStart(6)} | ${r.uncoveredLines}`);
    }
    console.log(sep);
    console.log(`${'all files'.padEnd(fileWidth)} | ${overallPct.toFixed(2).padStart(6)} |`);
    console.log(sep);
    console.log('ℹ end of C++ coverage report\n');
}

export async function runCoverage(rootDir) {
    const coveragePath = join(tmpdir(), `datasuite-cpp-coverage-${process.pid}.xml`);
    const exe = findOpenCppCoverage();

    const args = [
        '--sources', '*\\native\\src\\*',
        '--cover_children',
        '--optimized_build',
        '--export_type', `cobertura:${coveragePath}`,
        '--',
        'ctest', '--test-dir', 'dist/native-test', '-C', 'Release', '--output-on-failure'
    ];

    await new Promise((resolve, reject) => {
        // shell:true on Windows runs this through cmd.exe, which needs the
        // exe path quoted itself (not just args) when it contains spaces --
        // "C:\Program Files\OpenCppCoverage\...".
        const proc = spawn(`"${exe}"`, args, { stdio: 'inherit', shell: true, cwd: rootDir });
        proc.on('error', reject);
        proc.on('close', (code) => {
            if (code === 0) resolve();
            else reject(new Error(`OpenCppCoverage exited with code ${code}`));
        });
    });

    const xml = readFileSync(coveragePath, 'utf-8');
    unlinkSync(coveragePath);
    const files = parseCobertura(xml);
    printReport(files);
}
