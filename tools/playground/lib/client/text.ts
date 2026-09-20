/** Kernel help text can carry ANSI colors and man-page style overstrikes ("_\bx"). */
export function cleanHelpText(text: string): string {
    return text
        // eslint-disable-next-line no-control-regex
        .replace(/\u001b\[[0-9;]*[A-Za-z]/g, '')
        // eslint-disable-next-line no-control-regex
        .replace(/.\u0008/g, '')
        .trim();
}

export function commonPrefix(items: string[]): string {
    if (items.length === 0) return '';
    let prefix = items[0];
    for (const item of items.slice(1)) {
        let i = 0;
        while (i < prefix.length && i < item.length && prefix[i] === item[i]) i++;
        prefix = prefix.slice(0, i);
        if (!prefix) break;
    }
    return prefix;
}

export function formatBytes(bytes: number | null | undefined): string {
    if (bytes === null || bytes === undefined) return 'unknown';
    const mb = bytes / (1024 * 1024);
    return mb >= 1024 ? `${(mb / 1024).toFixed(2)} GB` : `${mb.toFixed(1)} MB`;
}

/**
 * Where an inspect request for "the thing at the cursor" should point: right
 * after a call's opening paren (`mean(|`) there is no symbol under the
 * cursor, but the function being called is what the user wants help on.
 */
export function inspectCursor(code: string, cursor: number): number {
    let pos = cursor;
    while (pos > 0 && /\s/.test(code[pos - 1])) pos--;
    if (pos > 0 && code[pos - 1] === '(') pos--;
    return pos;
}

/** True for a single identifier-like token (what double-click selects). */
export function isSingleToken(text: string): boolean {
    return /^[\w.$@:]+$/.test(text);
}
