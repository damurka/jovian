// The console transcript as plain data plus pure functions over it, so the
// exact same code rebuilds it from live iopub messages and from a
// hydrated history after a page refresh -- and can be unit-tested without a
// browser (test/transcript.test.ts).
import type { WireMessage } from './types.ts';

export type OutputClass = '' | 'stderr' | 'result' | 'error' | 'system';

/**
 * The most text one output block keeps. A cell that prints millions of lines
 * (for (i in 1:5e6) print(i)) would otherwise make the page lay out
 * gigabytes of text and freeze; past this the block keeps its tail and
 * shows a note that the start was dropped.
 */
export const MAX_OUTPUT_CHARS = 200_000;

export type Output =
    | { id: number; kind: 'text'; cls: OutputClass; text: string; displayId?: string; truncated?: boolean }
    | { id: number; kind: 'image'; cls: 'result'; src: string; displayId?: string }
    | { id: number; kind: 'input'; prompt: string; password: boolean; answered: boolean; answeredValue?: string };

export interface Cell {
    key: string;
    inNum: number;
    code: string;
    time: number;
    outputs: Output[];
    /** clear_output(wait=true): drop the old outputs when the next one arrives. */
    clearOnNext?: boolean;
}

export interface Transcript {
    cells: Cell[];
    /** The cell output messages currently attach to (the one being executed). */
    currentKey: string | null;
    nextOutputId: number;
}

export function emptyTranscript(): Transcript {
    return { cells: [], currentKey: null, nextOutputId: 1 };
}

export type MimeOutput =
    | { kind: 'image'; src: string }
    | { kind: 'text'; text: string };

/** Picks what to show from a mime bundle: an image if there is one, else text. */
export function formatMimeData(data: Record<string, any> | null | undefined): MimeOutput | null {
    if (!data) return null;
    if (data['image/png']) {
        const png = String(data['image/png']);
        return { kind: 'image', src: png.startsWith('data:') ? png : `data:image/png;base64,${png}` };
    }
    if (data['text/plain'] !== undefined) {
        const v = data['text/plain'];
        return { kind: 'text', text: Array.isArray(v) ? v.join('\n') : String(v) };
    }
    return { kind: 'text', text: JSON.stringify(data) };
}

/** Keeps the last MAX_OUTPUT_CHARS of `text`, starting at a line boundary when one is close. */
export function capText(text: string): { text: string; truncated: boolean } {
    if (text.length <= MAX_OUTPUT_CHARS) return { text, truncated: false };
    let kept = text.slice(text.length - MAX_OUTPUT_CHARS);
    const newline = kept.indexOf('\n');
    if (newline >= 0 && newline < 2000) kept = kept.slice(newline + 1);
    return { text: kept, truncated: true };
}

function mapCell(t: Transcript, key: string | null, fn: (cell: Cell) => Cell): Transcript {
    if (key === null) return t;
    let changed = false;
    const cells = t.cells.map((c) => {
        if (c.key !== key) return c;
        changed = true;
        return fn(c);
    });
    return changed ? { ...t, cells } : t;
}

/** Appends a cell and makes it the one output attaches to. */
export function startCell(t: Transcript, cell: Omit<Cell, 'outputs'>): Transcript {
    return { ...t, cells: [...t.cells, { ...cell, outputs: [] }], currentKey: cell.key };
}

export function endCell(t: Transcript): Transcript {
    return t.currentKey === null ? t : { ...t, currentKey: null };
}

export function clearCells(t: Transcript): Transcript {
    return { ...t, cells: [] };
}

/** Reconciles a cell's "In [N]" with the kernel's own authoritative count. */
export function setCellNumber(t: Transcript, key: string, inNum: number): Transcript {
    return mapCell(t, key, (c) => (c.inNum === inNum ? c : { ...c, inNum }));
}

type NewOutput =
    | { kind: 'text'; cls: OutputClass; text: string; displayId?: string; truncated?: boolean }
    | { kind: 'image'; cls: 'result'; src: string; displayId?: string }
    | { kind: 'input'; prompt: string; password: boolean; answered: boolean };

export function pushOutput(t: Transcript, output: NewOutput): Transcript {
    if (t.currentKey === null) return t;
    const id = t.nextOutputId;
    const next = mapCell(t, t.currentKey, (c) => {
        const outputs = c.clearOnNext ? [] : c.outputs;
        return { ...c, clearOnNext: false, outputs: [...outputs, { ...output, id } as Output] };
    });
    return next === t ? t : { ...next, nextOutputId: id + 1 };
}

/** Marks the current cell's pending input prompt as answered. */
export function answerInput(t: Transcript, value: string): Transcript {
    return mapCell(t, t.currentKey, (c) => ({
        ...c,
        outputs: c.outputs.map((o) =>
            o.kind === 'input' && !o.answered ? { ...o, answered: true, answeredValue: value } : o)
    }));
}

export function hasPendingInput(t: Transcript): boolean {
    const cell = t.cells.find((c) => c.key === t.currentKey);
    return Boolean(cell?.outputs.some((o) => o.kind === 'input' && !o.answered));
}

function withDisplay(mime: MimeOutput, displayId: string | undefined, cls: OutputClass): NewOutput {
    return mime.kind === 'image'
        ? { kind: 'image', cls: 'result', src: mime.src, displayId }
        : { kind: 'text', cls, text: mime.text, displayId };
}

/**
 * Applies one iopub/stdin message to the transcript: stream, execute_result,
 * display_data, update_display_data, clear_output, error and input_request.
 * Anything else (status, execute_input, replies, comms) changes nothing.
 */
export function applyMessage(t: Transcript, msg: WireMessage): Transcript {
    const content = msg.content ?? {};
    switch (msg.msgType) {
        case 'stream': {
            const cls: OutputClass = content.name === 'stderr' ? 'stderr' : '';
            const text: string = content.text ?? '';
            // A kernel writes one stream message per chunk (print("a", b)
            // is several); adjacent chunks of the same stream are one
            // block of text, not one box each.
            const cell = t.cells.find((c) => c.key === t.currentKey);
            const last = cell?.outputs.at(-1);
            if (cell && !cell.clearOnNext && last && last.kind === 'text' && last.cls === cls && !last.displayId) {
                const merged = capText(last.text + text);
                return mapCell(t, cell.key, (c) => ({
                    ...c,
                    outputs: [...c.outputs.slice(0, -1), { ...last, text: merged.text, truncated: last.truncated || merged.truncated }]
                }));
            }
            const first = capText(text);
            return pushOutput(t, { kind: 'text', cls, text: first.text, truncated: first.truncated });
        }

        case 'execute_result':
        case 'display_data': {
            const mime = formatMimeData(content.data);
            return mime ? pushOutput(t, withDisplay(mime, content.transient?.display_id, 'result')) : t;
        }

        case 'update_display_data': {
            // Replaces the display previously published under the same
            // transient.display_id -- which may sit in an EARLIER cell than
            // the one currently running.
            const mime = formatMimeData(content.data);
            if (!mime) return t;
            const displayId: string | undefined = content.transient?.display_id;
            if (displayId) {
                let found = false;
                const cells = t.cells.map((cell) => {
                    if (!cell.outputs.some((o) => o.kind !== 'input' && o.displayId === displayId)) return cell;
                    found = true;
                    return {
                        ...cell,
                        outputs: cell.outputs.map((o): Output => {
                            if (o.kind === 'input' || o.displayId !== displayId) return o;
                            return mime.kind === 'image'
                                ? { id: o.id, kind: 'image', cls: 'result', src: mime.src, displayId }
                                : { id: o.id, kind: 'text', cls: o.kind === 'text' ? o.cls : 'result', text: mime.text, displayId };
                        })
                    };
                });
                if (found) return { ...t, cells };
            }
            return pushOutput(t, withDisplay(mime, displayId, 'result'));
        }

        case 'clear_output':
            return mapCell(t, t.currentKey, (c) => (content.wait ? { ...c, clearOnNext: true } : { ...c, outputs: [] }));

        case 'error': {
            const traceback = Array.isArray(content.traceback) ? content.traceback.join('\n') : '';
            return pushOutput(t, {
                kind: 'text',
                cls: 'error',
                text: `${content.ename ?? 'Error'}: ${content.evalue ?? ''}${traceback ? '\n' + traceback : ''}`
            });
        }

        case 'input_request':
            return pushOutput(t, {
                kind: 'input',
                prompt: content.prompt || 'Input requested:',
                password: Boolean(content.password),
                answered: false
            });

        default:
            return t;
    }
}
