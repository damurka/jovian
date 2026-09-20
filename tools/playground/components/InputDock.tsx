'use client';

import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { api } from '@/lib/client/api';
import { commonPrefix, inspectCursor } from '@/lib/client/text';
import { tokenAt } from '@/lib/cursor';
import type { Anchor } from '@/lib/client/use-inspect';
import type { SessionView } from '@/lib/client/store';

interface Popup {
    items: string[];
    /** Range of the input the chosen item replaces (UTF-16 indices). */
    start: number;
    end: number;
    selected: number;
}

interface Props {
    session: SessionView;
    canRun: boolean;
    value: string;
    onChange: (value: string) => void;
    onRun: () => void;
    onInterrupt: () => void;
    onClear: () => void;
    onInspect: (title: string, code: string, cursor: number, anchor: Anchor, kernelBusy?: boolean) => void;
}

const MAX_ITEMS = 200;

/**
 * The code input dock. Besides running code it talks to the kernel:
 *   Tab            complete the token at the caret (or indent, at line start)
 *   Shift+Tab / Ctrl+I   inspect the symbol at the caret
 * Both are real complete_request / inspect_request round trips.
 */
export function InputDock({ session, canRun, value, onChange, onRun, onInterrupt, onClear, onInspect }: Props) {
    const textareaRef = useRef<HTMLTextAreaElement>(null);
    const areaRef = useRef<HTMLDivElement>(null);
    const [popup, setPopup] = useState<Popup | null>(null);
    const [hint, setHint] = useState('');
    const requestSeq = useRef(0);
    const pendingCaret = useRef<number | null>(null);
    const hintTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

    // `sticky` hints stay until replaced (used while waiting for a busy kernel).
    const showHint = useCallback((text: string, sticky = false) => {
        setHint(text);
        clearTimeout(hintTimer.current);
        if (!sticky) hintTimer.current = setTimeout(() => setHint(''), 3000);
    }, []);

    useEffect(() => () => clearTimeout(hintTimer.current), []);

    // Restores the caret after a programmatic edit (React re-renders the
    // textarea's value, which would otherwise jump the caret to the end).
    useLayoutEffect(() => {
        if (pendingCaret.current !== null && textareaRef.current) {
            textareaRef.current.setSelectionRange(pendingCaret.current, pendingCaret.current);
            pendingCaret.current = null;
        }
    });

    // A different session has different symbols: drop any open popup.
    useEffect(() => {
        requestSeq.current++;
        setPopup(null);
    }, [session.id]);

    const edit = (start: number, end: number, replacement: string) => {
        onChange(value.slice(0, start) + replacement + value.slice(end));
        pendingCaret.current = start + replacement.length;
    };

    const accept = (p: Popup, index = p.selected) => {
        edit(p.start, p.end, p.items[index]);
        setPopup(null);
    };

    const requestCompletion = async () => {
        const ta = textareaRef.current;
        if (!ta) return;
        const cursor = ta.selectionStart;
        const code = value;
        const mine = ++requestSeq.current;

        showHint(session.running ? 'Kernel is running code — completing when it finishes…' : 'Completing…', session.running);
        let result;
        try {
            result = await api.complete(session.id, code, cursor);
        } catch (error) {
            if (mine === requestSeq.current) showHint(`Completion failed: ${(error as Error).message}`);
            return;
        }
        // The user typed on (or asked again) while this was in flight.
        if (mine !== requestSeq.current || textareaRef.current?.value !== code) return;

        if (result.busy) return showHint('The kernel did not answer in time — try again.');
        if (!result.ok) return showHint(result.error ?? 'Completion failed.');
        const items = (result.matches ?? []).slice(0, MAX_ITEMS);
        if (items.length === 0) return showHint('No completions.');
        setHint('');

        // The kernel says which range to replace; fall back to the token at
        // the caret if it sent something nonsensical.
        let start = result.cursorStart ?? cursor;
        let end = result.cursorEnd ?? cursor;
        if (start < 0 || end > code.length || start > end) {
            const t = tokenAt(code, cursor);
            start = t.start;
            end = t.end;
        }

        if (items.length === 1) {
            edit(start, end, items[0]);
            return;
        }
        // Several candidates: extend to their common prefix (like a shell),
        // then let the user pick.
        const prefix = commonPrefix(items);
        if (prefix.length > end - start) {
            edit(start, end, prefix);
            end = start + prefix.length;
        }
        setPopup({ items, start, end, selected: 0 });
    };

    const requestInspect = () => {
        const ta = textareaRef.current;
        if (!ta || !areaRef.current) return;
        setPopup(null);
        const cursor = inspectCursor(value, ta.selectionStart);
        const token = tokenAt(value, cursor).token;
        const rect = areaRef.current.getBoundingClientRect();
        onInspect(token || 'symbol at cursor', value, cursor, {
            kind: 'above',
            left: rect.left,
            bottom: window.innerHeight - rect.top + 8
        }, session.running);
    };

    const indent = () => (session.kernelType === 'python' ? '    ' : '  ');

    const onKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
        const plainTab = e.key === 'Tab' && !e.shiftKey && !e.ctrlKey && !e.altKey && !e.metaKey;

        if (popup) {
            if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                e.preventDefault();
                const step = e.key === 'ArrowDown' ? 1 : -1;
                setPopup({ ...popup, selected: (popup.selected + step + popup.items.length) % popup.items.length });
                return;
            }
            if ((e.key === 'Enter' && !e.shiftKey && !e.ctrlKey && !e.metaKey) || plainTab) {
                e.preventDefault();
                accept(popup);
                return;
            }
            if (e.key === 'Escape') {
                e.preventDefault();
                setPopup(null);
                return;
            }
            if (!['Shift', 'Control', 'Alt', 'Meta'].includes(e.key)) setPopup(null);
        }

        if (e.key === 'Escape' && session.running) {
            e.preventDefault();
            onInterrupt();
        } else if (e.ctrlKey && !e.shiftKey && !e.altKey && e.key.toLowerCase() === 'l') {
            e.preventDefault();
            onClear();
        } else if (e.key === 'Enter' && (e.shiftKey || e.ctrlKey || e.metaKey)) {
            e.preventDefault();
            onRun();
        } else if (plainTab) {
            e.preventDefault();
            const ta = e.currentTarget;
            const before = value.slice(0, ta.selectionStart);
            const line = before.slice(before.lastIndexOf('\n') + 1);
            if (line.trim() === '') {
                edit(ta.selectionStart, ta.selectionEnd, indent());
            } else if (canRun) {
                void requestCompletion();
            }
        } else if ((e.key === 'Tab' && e.shiftKey) || (e.ctrlKey && e.key.toLowerCase() === 'i')) {
            e.preventDefault();
            if (canRun) requestInspect();
        }
    };

    // Keep the highlighted item in view while arrowing through a long list.
    useEffect(() => {
        areaRef.current?.querySelector('.completion-item.selected')?.scrollIntoView({ block: 'nearest' });
    }, [popup?.selected]);

    return (
        <div className="repl-input-dock" id="replDock">
            <div className="input-row">
                <div className="prompt-label">In [{session.execCount + 1}]:</div>
                <div className="code-editor-area" ref={areaRef}>
                    {popup && (
                        <div className="completion-popup" role="listbox">
                            {popup.items.map((item, i) => (
                                <div
                                    key={item + i}
                                    role="option"
                                    aria-selected={i === popup.selected}
                                    className={'completion-item' + (i === popup.selected ? ' selected' : '')}
                                    // mousedown, not click: click would blur the textarea first.
                                    onMouseDown={(e) => { e.preventDefault(); accept(popup, i); }}
                                >
                                    <span className="match-prefix">{item.slice(0, popup.end - popup.start)}</span>
                                    {item.slice(popup.end - popup.start)}
                                </div>
                            ))}
                            <div className="completion-footer">↑↓ choose · Enter/Tab accept · Esc close</div>
                        </div>
                    )}
                    <textarea
                        ref={textareaRef}
                        className="repl-input"
                        value={value}
                        placeholder="Enter code for the active kernel..."
                        spellCheck={false}
                        onChange={(e) => { setPopup(null); setHint(''); requestSeq.current++; onChange(e.target.value); }}
                        onKeyDown={onKeyDown}
                        onBlur={() => setPopup(null)}
                    />
                </div>
            </div>

            <div className="input-actions">
                <div className="action-shortcuts">
                    <span><span className="kbd-key">Shift</span> + <span className="kbd-key">Enter</span> Run Cell</span>
                    <span><span className="kbd-key">Tab</span> Complete</span>
                    <span><span className="kbd-key">Shift</span> + <span className="kbd-key">Tab</span> Inspect</span>
                    <span><span className="kbd-key">Ctrl</span> + <span className="kbd-key">L</span> Clear</span>
                    <span><span className="kbd-key">Esc</span> Interrupt</span>
                    <span className="toast-line">{hint}</span>
                </div>
                <button className="btn-primary" id="btnExecute" disabled={!canRun || session.running} onClick={onRun}>
                    <span>&#9654;</span> Run
                </button>
            </div>
        </div>
    );
}
