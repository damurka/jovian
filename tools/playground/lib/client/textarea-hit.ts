// Which word is under the mouse in a <textarea>? A textarea has no per-word
// DOM to hit-test, so this lays the same text out in an invisible <div> with
// the textarea's exact metrics and asks the browser for each word's boxes.

export interface TokenHit {
    token: string;
    /** UTF-16 range of the token in the textarea's value. */
    start: number;
    end: number;
    /** The token's box in viewport coordinates. */
    rect: { left: number; right: number; top: number; bottom: number };
}

const TOKEN = /[\w.$@:]+/g;

export function tokenAtPoint(textarea: HTMLTextAreaElement, clientX: number, clientY: number): TokenHit | null {
    const value = textarea.value;
    if (!value) return null;

    const style = getComputedStyle(textarea);
    const box = textarea.getBoundingClientRect();
    const paddingLeft = parseFloat(style.paddingLeft) || 0;
    const paddingRight = parseFloat(style.paddingRight) || 0;

    const mirror = document.createElement('div');
    Object.assign(mirror.style, {
        position: 'fixed',
        visibility: 'hidden',
        pointerEvents: 'none',
        left: `${box.left + textarea.clientLeft}px`,
        top: `${box.top + textarea.clientTop - textarea.scrollTop}px`,
        boxSizing: 'content-box',
        width: `${textarea.clientWidth - paddingLeft - paddingRight}px`,
        padding: style.padding,
        border: '0',
        font: style.font,
        letterSpacing: style.letterSpacing,
        lineHeight: style.lineHeight,
        tabSize: style.tabSize,
        whiteSpace: 'pre-wrap',
        overflowWrap: 'break-word'
    } satisfies Partial<CSSStyleDeclaration>);
    mirror.textContent = value;
    document.body.appendChild(mirror);

    try {
        const text = mirror.firstChild;
        if (!text) return null;
        const range = document.createRange();
        TOKEN.lastIndex = 0;
        let match: RegExpExecArray | null;
        while ((match = TOKEN.exec(value)) !== null) {
            range.setStart(text, match.index);
            range.setEnd(text, match.index + match[0].length);
            for (const r of Array.from(range.getClientRects())) {
                if (clientX >= r.left && clientX <= r.right && clientY >= r.top && clientY <= r.bottom) {
                    return {
                        token: match[0],
                        start: match.index,
                        end: match.index + match[0].length,
                        rect: { left: r.left, right: r.right, top: r.top, bottom: r.bottom }
                    };
                }
            }
        }
        return null;
    } finally {
        mirror.remove();
    }
}

/** The word under the pointer in ordinary DOM text (a cell's echoed code), or null. */
export function tokenAtDomPoint(clientX: number, clientY: number): { token: string; rect: DOMRect } | null {
    const doc = document as Document & {
        caretPositionFromPoint?: (x: number, y: number) => { offsetNode: Node; offset: number } | null;
        caretRangeFromPoint?: (x: number, y: number) => Range | null;
    };
    let node: Node | null = null;
    let offset = 0;
    if (doc.caretPositionFromPoint) {
        const pos = doc.caretPositionFromPoint(clientX, clientY);
        if (pos) { node = pos.offsetNode; offset = pos.offset; }
    } else if (doc.caretRangeFromPoint) {
        const range = doc.caretRangeFromPoint(clientX, clientY);
        if (range) { node = range.startContainer; offset = range.startOffset; }
    }
    if (!node || node.nodeType !== Node.TEXT_NODE) return null;

    const data = (node as Text).data;
    TOKEN.lastIndex = 0;
    let match: RegExpExecArray | null;
    while ((match = TOKEN.exec(data)) !== null) {
        if (offset < match.index || offset > match.index + match[0].length) continue;
        // The caret APIs snap to the nearest character even when the pointer
        // is far from it (blank space at a line's end): confirm it is really over the word.
        const range = document.createRange();
        range.setStart(node, match.index);
        range.setEnd(node, match.index + match[0].length);
        for (const r of Array.from(range.getClientRects())) {
            if (clientX >= r.left && clientX <= r.right && clientY >= r.top && clientY <= r.bottom) {
                return { token: match[0], rect: r };
            }
        }
        return null;
    }
    return null;
}
