'use client';

import { useEffect, useRef, useState } from 'react';
import { isSingleToken } from '@/lib/client/text';
import type { SessionView } from '@/lib/client/store';
import { MAX_OUTPUT_CHARS, type Output } from '@/lib/transcript';

export function kernelLabel(kernelType: string): string {
    return kernelType === 'python' ? 'Python (Carpo)' : 'R (Elara)';
}

function InputRequest({ output, disabled, onSubmit }: {
    output: Extract<Output, { kind: 'input' }>;
    disabled: boolean;
    onSubmit: (value: string) => void;
}) {
    const [value, setValue] = useState('');
    const fieldRef = useRef<HTMLInputElement>(null);

    // The kernel is blocked until this is answered, so take focus.
    useEffect(() => {
        if (!output.answered) fieldRef.current?.focus();
    }, [output.answered]);

    const submit = () => {
        if (!output.answered && !disabled) onSubmit(value);
    };

    return (
        <div className="output-section input-request">
            <div className="input-request-row">
                <span className="prompt-label">{output.prompt}</span>
                <input
                    ref={fieldRef}
                    type={output.password ? 'password' : 'text'}
                    className="input-request-field"
                    disabled={output.answered}
                    value={output.answered ? (output.password ? '••••••' : output.answeredValue ?? '') : value}
                    onChange={(e) => setValue(e.target.value)}
                    onKeyDown={(e) => {
                        if (e.key === 'Enter') { e.preventDefault(); submit(); }
                    }}
                />
                {!output.answered && <button className="btn-primary" onClick={submit}>Send</button>}
            </div>
        </div>
    );
}

interface Props {
    session: SessionView | null;
    hasSessions: boolean;
    onAnswerInput: (value: string) => void;
    /** Inspect a symbol the user picked out of the transcript. */
    onInspect: (token: string, x: number, y: number) => void;
}

/** The scrolling transcript: one entry per execution, code above its output. */
export function ConsoleView({ session, hasSessions, onAnswerInput, onInspect }: Props) {
    const containerRef = useRef<HTMLDivElement>(null);
    const [chip, setChip] = useState<{ x: number; y: number; text: string } | null>(null);

    const cells = session?.transcript.cells ?? [];
    const outputCount = cells.reduce((n, c) => n + c.outputs.length, 0);

    useEffect(() => {
        const el = containerRef.current;
        if (el) el.scrollTop = el.scrollHeight;
    }, [session?.id, cells.length, outputCount]);

    // The "Inspect" chip belongs to a selection: drop it when the user
    // clicks elsewhere or the selection goes away.
    useEffect(() => {
        if (!chip) return;
        const onDown = (e: MouseEvent) => {
            if (!(e.target as HTMLElement)?.closest?.('.inspect-chip')) setChip(null);
        };
        window.addEventListener('mousedown', onDown);
        return () => window.removeEventListener('mousedown', onDown);
    }, [chip]);

    // Inspect from the output window: a double-click on a word inspects that
    // word straight away; dragging out a longer selection offers an Inspect
    // chip instead (an unprompted popover on every text selection -- e.g.
    // one made only to copy it -- would be a nuisance).
    const onMouseUp = (e: React.MouseEvent) => {
        if (!session || (e.target as HTMLElement).closest('.input-request, input, button')) return;
        const selection = window.getSelection();
        if (!selection || selection.isCollapsed || !containerRef.current?.contains(selection.anchorNode)) {
            setChip(null);
            return;
        }
        const text = selection.toString().trim();
        if (!text || text.includes('\n') || text.length > 120) {
            setChip(null);
            return;
        }
        if (e.detail >= 2 && isSingleToken(text)) {
            setChip(null);
            onInspect(text, e.clientX, e.clientY);
        } else {
            setChip({ x: e.clientX, y: e.clientY, text });
        }
    };

    if (!session) {
        return (
            <div className="repl-container" id="replOutput">
                <div className="empty-state">
                    {hasSessions
                        ? 'Select a session from the sidebar to see its console.'
                        : <>No sessions yet. Click <strong>+ New Kernel Session</strong> above to spawn a real R or Python kernel process.</>}
                </div>
            </div>
        );
    }

    return (
        <div className="repl-container" id="replOutput" ref={containerRef} onMouseUp={onMouseUp}>
            {cells.length === 0 && (
                <div className="empty-state">No output yet. Run some code below, or pick a preset from the right panel.</div>
            )}
            {cells.map((cell) => (
                <div className="repl-entry" key={cell.key}>
                    <div className="repl-header">
                        <span><span className="in-prompt">In [{cell.inNum}]:</span> {kernelLabel(session.kernelType)}</span>
                        <span>{new Date(cell.time).toLocaleTimeString()}</span>
                    </div>
                    <div className="code-input-preview">{cell.code}</div>
                    {cell.outputs.map((out) => {
                        if (out.kind === 'input') {
                            return (
                                <InputRequest
                                    key={out.id}
                                    output={out}
                                    disabled={session.status === 'stopped' || session.status === 'crashed'}
                                    onSubmit={onAnswerInput}
                                />
                            );
                        }
                        if (out.kind === 'image') {
                            return (
                                <div className="output-section result" key={out.id}>
                                    {/* eslint-disable-next-line @next/next/no-img-element */}
                                    <img src={out.src} alt="plot" />
                                </div>
                            );
                        }
                        return (
                            <div className={`output-section ${out.cls}`} key={out.id}>
                                {out.truncated && (
                                    <div className="output-truncated">… earlier output not shown: only the last {MAX_OUTPUT_CHARS.toLocaleString()} characters are kept …</div>
                                )}
                                {out.text}
                            </div>
                        );
                    })}
                </div>
            ))}
            {chip && (
                <button
                    className="inspect-chip"
                    style={{ left: Math.min(chip.x + 8, window.innerWidth - 90), top: chip.y + 10 }}
                    onMouseDown={(e) => e.preventDefault()}
                    onClick={() => { onInspect(chip.text, chip.x, chip.y); setChip(null); }}
                >
                    ⓘ Inspect
                </button>
            )}
        </div>
    );
}
