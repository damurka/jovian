import { test } from 'node:test';
import * as assert from 'node:assert';
import { inspectableToken, isInspectableToken, shouldAutoComplete } from '../lib/client/autotrigger.ts';

test('typing a second identifier character triggers completion at the end of the token', () => {
    assert.strictEqual(shouldAutoComplete('pr', 2), true);
    assert.strictEqual(shouldAutoComplete('x <- mea', 8), true);
    assert.strictEqual(shouldAutoComplete('p', 1), false, 'one character is too little to be useful');
});

test('accessors trigger completion straight away', () => {
    assert.strictEqual(shouldAutoComplete('df$', 3), true);
    assert.strictEqual(shouldAutoComplete('stats::', 7), true);
    assert.strictEqual(shouldAutoComplete('os.', 3), true);
});

test('no completion in the middle of a word, on numbers, or after whitespace', () => {
    assert.strictEqual(shouldAutoComplete('print(x)', 3), false, 'caret inside print');
    assert.strictEqual(shouldAutoComplete('123', 3), false);
    assert.strictEqual(shouldAutoComplete('x <- ', 5), false);
    assert.strictEqual(shouldAutoComplete('', 0), false);
});

test('only identifier-like words of two or more characters are inspectable', () => {
    assert.strictEqual(isInspectableToken('mean'), true);
    assert.strictEqual(isInspectableToken('stats::sd'), true);
    assert.strictEqual(isInspectableToken('os.path.join'), true);
    assert.strictEqual(isInspectableToken('x'), false);
    assert.strictEqual(isInspectableToken('42'), false);
    assert.strictEqual(isInspectableToken('1e5'), false);
    assert.strictEqual(inspectableToken('df$'), 'df');
    assert.strictEqual(inspectableToken('mean'), 'mean');
});
