#!/usr/bin/env node
import { spawn } from 'child_process';
import { existsSync, readFileSync, writeFileSync } from 'fs';
import { glob } from 'glob';

const ROOT = process.cwd();
const isDryRun = process.argv.includes('--dry-run');

async function run(cmd, args) {
    return new Promise((resolve, reject) => {
        const proc = spawn(cmd, args, {
            stdio: 'inherit',
            shell: true,
            cwd: ROOT
        });

        proc.on('close', (code) => {
            if (code === 0) resolve();
            else reject(new Error(`Command failed with code ${code}`));
        });
    });
}

async function formatCpp() {
    const files = await glob('native/{include,src,test}/**/*.{cpp,hpp,h}', { cwd: ROOT });
    if (files.length === 0) {
        console.log('  (no C++ files found)');
        return;
    }

    // Without a .clang-format, clang-format would use its built-in (LLVM)
    // style and rewrite the whole native tree into it: skipped until the
    // house style is written down.
    if (!existsSync('.clang-format')) {
        console.log('  ⚠️  No .clang-format found -- C++ is left as it is.');
        return;
    }

    const args = isDryRun
        ? ['--dry-run', '--Werror', ...files]
        : ['-i', ...files];

    await run('npx', ['clang-format', ...args]);
}

// VS Code's TypeScript formatting (its build/lib/formatter.ts and src/tsfmt.json): the TypeScript language service's
// formatter, the editor's "Format Document" -- with Jovian's 4 spaces where VS Code indents with tabs.
const TS_FORMAT = {
    baseIndentSize: 0,
    indentSize: 4,
    tabSize: 4,
    convertTabsToSpaces: true,
    insertSpaceAfterCommaDelimiter: true,
    insertSpaceAfterSemicolonInForStatements: true,
    insertSpaceBeforeAndAfterBinaryOperators: true,
    insertSpaceAfterKeywordsInControlFlowStatements: true,
    insertSpaceAfterFunctionKeywordForAnonymousFunctions: true,
    insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis: false,
    insertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets: false,
    insertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces: false,
    insertSpaceAfterOpeningAndBeforeClosingEmptyBraces: true,
    insertSpaceBeforeFunctionParenthesis: false,
    placeOpenBraceOnNewLineForFunctions: false,
    placeOpenBraceOnNewLineForControlBlocks: false
};

/** The files whose formatting differs from VS Code's; written formatted unless --dry-run. */
async function formatTsFiles(files) {
    // the TypeScript 6 API (package "typescript"); the compiler is TypeScript 7 (@typescript/native), which has none
    const ts = (await import('typescript')).default;
    const texts = new Map(files.map((file) => [file, readFileSync(file, 'utf8')]));
    const service = ts.createLanguageService({
        getCompilationSettings: () => ts.getDefaultCompilerOptions(),
        getScriptFileNames: () => files,
        getScriptVersion: () => '0',
        getScriptSnapshot: (file) => texts.has(file) ? ts.ScriptSnapshot.fromString(texts.get(file)) : undefined,
        getCurrentDirectory: () => ROOT,
        getDefaultLibFileName: (options) => ts.getDefaultLibFilePath(options),
        fileExists: (file) => texts.has(file),
        readFile: (file) => texts.get(file)
    });
    const changed = [];
    for (const file of files) {
        const original = texts.get(file);
        const settings = { ...TS_FORMAT, newLineCharacter: original.includes('\r\n') ? '\r\n' : '\n' };
        let text = original;
        const edits = [...service.getFormattingEditsForDocument(file, settings)].sort((a, b) => b.span.start - a.span.start);
        for (const edit of edits) {
            text = text.slice(0, edit.span.start) + edit.newText + text.slice(edit.span.start + edit.span.length);
        }
        if (text !== original) {
            changed.push(file);
            if (!isDryRun) writeFileSync(file, text);
        }
    }
    return changed;
}

async function formatTs() {
    const files = (await glob('{lib,test,scripts}/**/*.{ts,mts,cts}', { cwd: ROOT, ignore: ['**/node_modules/**', '**/*.d.ts'] })).sort();
    const changed = await formatTsFiles(files);
    if (changed.length) {
        console.log(`  ${isDryRun ? 'Not formatted' : 'Formatted'}: ${changed.join(', ')}`);
    }

    // then ESLint's rules (eslint.config.mjs), fixing what it can
    await run('npx', ['eslint', '"lib/**/*.ts"', '"test/**/*.ts"', ...(isDryRun ? [] : ['--fix'])]);
    if (isDryRun && changed.length) {
        throw new Error(`${changed.length} file(s) not formatted: run npm run format`);
    }
}

async function main() {
    console.log(isDryRun ? '🔍 Checking formatting...\n' : '🎨 Formatting...\n');

    console.log('📦 C++ (clang-format)...');
    await formatCpp();
    console.log('✅ C++ done\n');

    console.log('📦 TypeScript (VS Code\'s formatter, eslint)...');
    await formatTs();
    console.log('✅ TypeScript done\n');

    console.log(isDryRun ? '🎉 Formatting check complete!' : '🎉 Formatting complete!');
}

main().catch((err) => {
    console.error(`❌ Format${isDryRun ? ' check' : ''} failed:`, err.message);
    process.exit(1);
});
