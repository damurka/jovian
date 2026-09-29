import { test } from 'node:test';
import * as assert from 'node:assert';
import { analyzeR } from '../../../dist/lib/session/r-static.js';

const defines = (code: string) => analyzeR(code).defines;
const packages = (code: string) => analyzeR(code).packages;

test('analyzeR: names the code defines', async (t) => {
    await t.test('every assignment form', () => {
        assert.deepStrictEqual(defines('a <- 1\nb <<- 2\nc = 3\n4 -> d\n5 ->> e'), ['a', 'b', 'c', 'd', 'e']);
    });

    await t.test('function arguments, loop variables and assign()', () => {
        assert.deepStrictEqual(defines('f <- function(x, y = 2, ...) x + y'), ['f', 'x', 'y']);
        assert.deepStrictEqual(defines('for (row in 1:10) print(row)'), ['row']);
        assert.deepStrictEqual(defines('assign("made_up", 1)'), ['made_up']);
    });

    await t.test('an = inside a call names an argument, not a variable', () => {
        assert.deepStrictEqual(defines('mean(x = 1:3)\nd[i = 1]'), []);
        assert.deepStrictEqual(defines('{ inside = 1 }'), ['inside']);
    });

    await t.test('not the member of a$b <- ..., and not keywords', () => {
        assert.deepStrictEqual(defines('obj$field <- 1\nobj@slot <- 2'), []);
        assert.deepStrictEqual(defines('TRUE <- 1'), []);
    });

    await t.test('backtick-quoted and string targets', () => {
        assert.deepStrictEqual(defines('`my var` <- 1\n"quoted" <- 2'), ['my var', 'quoted']);
    });

    await t.test('ignores comments and strings that look like code', () => {
        assert.deepStrictEqual(defines('# not <- 1\nx <- "y <- 2"\nr"(z <- 3)"'), ['x']);
    });

    await t.test('lists each name once, in order, and survives unfinished code', () => {
        assert.deepStrictEqual(defines('a <- 1\na <- 2\nb <- f(a'), ['a', 'b']);
        assert.deepStrictEqual(defines('x <- "unterminated'), ['x']);
    });
});

test('analyzeR: packages the code attaches', async (t) => {
    await t.test('library() and require(), quoted or not, once each', () => {
        assert.deepStrictEqual(packages('library(purrr)\nrequire("dplyr")\nlibrary(package = ggplot2)\nlibrary(purrr)'), ['purrr', 'dplyr', 'ggplot2']);
    });

    await t.test('not in comments or strings, and not a method called library', () => {
        assert.deepStrictEqual(packages('# library(nope)\nx <- "library(nope)"\nobj$library(nope)'), []);
    });
});
