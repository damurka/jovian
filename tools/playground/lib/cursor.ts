// Jupyter cursor positions count Unicode code points; JavaScript string
// indices count UTF-16 code units. They only differ for characters outside
// the Basic Multilingual Plane (emoji, some CJK), but a completion that
// lands one character off is a confusing bug, so convert at the boundary.

/** UTF-16 index -> code point index. */
export function toCodePointIndex(text: string, utf16Index: number): number {
    return Array.from(text.slice(0, utf16Index)).length;
}

/** Code point index -> UTF-16 index. */
export function toUtf16Index(text: string, codePointIndex: number): number {
    return Array.from(text).slice(0, codePointIndex).join('').length;
}

/**
 * The identifier-ish token the cursor sits in or right after -- what an
 * inspect request on a bare word should look at. Includes `.`, `$`, `@`,
 * `:` so `dplyr::filter`, `df$col`, `os.path.join` resolve as a whole.
 */
export function tokenAt(text: string, utf16Index: number): { token: string; start: number; end: number } {
    const isTokenChar = (ch: string) => /[\w.$@:]/.test(ch);
    let start = Math.min(utf16Index, text.length);
    let end = start;
    while (start > 0 && isTokenChar(text[start - 1])) start--;
    while (end < text.length && isTokenChar(text[end])) end++;
    return { token: text.slice(start, end), start, end };
}
