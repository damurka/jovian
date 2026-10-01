// Records what an R session sends for a set of cells -- streams, results, plots, errors -- as normalised JSON, so the
// output of two hera versions can be compared message for message.
//
//   node examples/r-execute-cases.ts > before.json
//
// Uses the built supervisor and kernel (dist/), like the integration tests.

import { SessionManager } from '../dist/lib/index.js';
import { discoverRHome } from '../dist/lib/session/runtimes.js';

const CASES: Array<{ name: string; code: string; silent?: boolean }> = [
    { name: 'value', code: '1 + 1' },
    { name: 'assignment', code: 'x <- 5' },
    { name: 'invisible', code: 'invisible(3)' },
    { name: 'two values', code: '1\n2' },
    { name: 'NULL value', code: 'NULL' },
    { name: 'print and cat', code: 'print("a")\ncat("b\\n")' },
    { name: 'output then value', code: 'print(1:3)\n10' },
    { name: 'unicode', code: 'cat("é ü 日本\\n")' },
    { name: 'message', code: 'message("m")' },
    { name: 'message in function', code: 'f <- function() { message("hi"); 1 }\nf()' },
    { name: 'top-level warning', code: 'warning("w")' },
    { name: 'warning in function', code: 'fw <- function() warning("fw")\nfw()' },
    { name: 'warnings in a loop', code: 'for (i in 1:2) warning(i)' },
    { name: 'warning then value', code: 'warning("w2")\n5' },
    { name: 'suppressed warning', code: 'suppressWarnings(warning("x"))' },
    { name: 'C-level warning', code: 'as.integer("a")' },
    { name: 'stop', code: 'stop("boom")' },
    { name: 'nested stop', code: 'f1 <- function() stop("deep")\ng1 <- function() f1()\ng1()' },
    { name: 'C-level error', code: 'log(-"a")' },
    { name: 'error stops the cell', code: 'cat("a\\n")\nstop("x")\ncat("b\\n")' },
    { name: 'caught error', code: 'tryCatch(stop("e"), error = function(e) cat("caught\\n"))' },
    { name: 'try', code: 'try(stop("t"))' },
    { name: 'rlang error', code: 'rlang::abort("rl")' },
    { name: 'parse error', code: '1 +* 2' },
    { name: 'incomplete code', code: 'f <- function() {' },
    { name: '.Last.value', code: '42' },
    { name: '.Last.value next cell', code: '.Last.value + 1' },
    { name: 'plot', code: 'plot(1:10)' },
    { name: 'two plots', code: 'plot(1)\nplot(2)' },
    { name: 'plot built up', code: 'plot(1:10)\nabline(h = 5)' },
    { name: 'mfrow', code: 'par(mfrow = c(1, 2))\nplot(1)\nplot(2)' },
    { name: 'invisible plot', code: 'invisible(plot(1))' },
    { name: 'plot then error', code: 'plot(1)\nstop("after plot")' },
    { name: 'no plot carried over', code: 'abline(h = 1)' },
    { name: 'ggplot value', code: 'if (requireNamespace("ggplot2", quietly = TRUE)) ggplot2::ggplot(mtcars, ggplot2::aes(mpg, wt)) + ggplot2::geom_point() else "no ggplot2"' },
    { name: 'data frame', code: 'head(mtcars, 2)' },
    { name: 'plot size option', code: 'options(repr.plot.width = 4, repr.plot.height = 3)\nplot(1)\noptions(repr.plot.width = NULL, repr.plot.height = NULL)' },
    { name: 'cell_options', code: 'hera::cell_options(digits = 3)\npi' },
    { name: 'cell_options gone', code: 'pi' },
    { name: 'silent output', code: 'cat("hidden\\n")\nmessage("hidden")\n1', silent: true },
    { name: 'silent error', code: 'stop("silent boom")', silent: true },
    { name: 'help', code: 'class(help("mean"))' },
];

type Out = Record<string, unknown>;

function normalise(message: any): Out | undefined {
    const content = message.content ?? {};
    switch (message.msgType) {
        case 'stream':
            return { stream: content.name, text: content.text };
        case 'execute_result':
        case 'display_data': {
            const data: Out = {};
            for (const [mime, value] of Object.entries(content.data ?? {})) {
                data[mime] = typeof value === 'string' && /^image\/(png|jpeg)$/.test(mime) ? `<${mime} ${value.length} chars>` : value;
            }
            return { [message.msgType]: data, metadata: content.metadata };
        }
        case 'error':
            return { error: content.ename, evalue: content.evalue, traceback: content.traceback };
        case 'clear_output':
            return { clear_output: content.wait };
        default:
            return undefined;
    }
}

const manager = new SessionManager();
const session = await manager.createSession({ rHome: await discoverRHome() });
session.on('error', () => { /* recorded from the result instead */ });
const results: Out[] = [];
try {
    // inspection (Shift-Tab): a function's help, a value, a data frame, a factor, a name that doesn't exist
    await session.execute('v <- 1:3\nfac <- factor(c("a", "b"))');
    for (const code of ['mean', 'v', 'mtcars', 'fac', 'no_such_thing']) {
        const reply: any = await session.inspect(code);
        const data: Out = {};
        for (const [mime, value] of Object.entries(reply.data ?? {})) {
            data[mime] = typeof value === 'string' ? value : JSON.stringify(value);
        }
        results.push({ inspect: code, found: reply.found, data });
    }
    const early = await session.execute('cat(sort(loadedNamespaces()))');
    results.push({ loadedAfterInspect: (early.output ?? []).filter((m: any) => m.msgType === 'stream').map((m: any) => m.content.text).join('') });
    for (const c of CASES) {
        const result = await session.execute(c.code, { silent: c.silent, timeout: 60_000 });
        // stream chunks may be split differently from run to run: join consecutive ones of the same stream
        const outputs: Out[] = [];
        for (const message of result.output ?? []) {
            const out = normalise(message);
            if (!out) continue;
            const last = outputs[outputs.length - 1];
            if (out.stream && last?.stream === out.stream) {
                last.text = String(last.text) + String(out.text);
            } else {
                outputs.push(out);
            }
        }
        results.push({ case: c.name, success: result.success, ...(result.success ? {} : { ename: result.error?.ename, evalue: result.error?.evalue }), outputs });
    }
    // what the session has loaded once all of this was shown
    const loaded = await session.execute('cat(sort(loadedNamespaces()))');
    results.push({ loaded: (loaded.output ?? []).filter((m: any) => m.msgType === 'stream').map((m: any) => m.content.text).join('') });
} finally {
    await manager.stopAll();
}
console.log(JSON.stringify(results, null, 1));
