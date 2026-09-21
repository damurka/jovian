// When the UI should ask the kernel on its own (no Tab / Shift+Tab). Pure, so
// the rules can be unit-tested.
import { tokenAt } from '../cursor.ts';

/** A word worth asking the kernel about: an identifier-like token, not a number or a lone character. */
export function isInspectableToken(token: string): boolean {
    const trimmed = token.replace(/[.:$@]+$/, '');
    return trimmed.length >= 2 && /^[A-Za-z._][\w.$@:]*$/.test(trimmed);
}

/** The token without trailing accessor punctuation ("df$" -> "df"), what inspect should look up. */
export function inspectableToken(token: string): string {
    return token.replace(/[.:$@]+$/, '');
}

/**
 * Whether an edit that left the caret at `caret` should pop up completions by
 * itself: the caret is at the end of a token that starts like an identifier
 * and is at least two characters, or that ends in an accessor (`df$`, `pkg::`,
 * `os.`) which is when completions are most useful.
 */
export function shouldAutoComplete(value: string, caret: number): boolean {
    if (caret <= 0 || caret > value.length) return false;
    const { token, end } = tokenAt(value, caret);
    if (end !== caret) return false; // the caret is in the middle of a word
    if (!/^[A-Za-z._]/.test(token)) return false;
    if (/[$@]$|::$/.test(token)) return token.length >= 2;
    return token.length >= 2;
}
