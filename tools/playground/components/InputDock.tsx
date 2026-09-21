'use client';

import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { api } from '@/lib/client/api';
import { inspectableToken, isInspectableToken, shouldAutoComplete } from '@/lib/client/autotrigger';
import { commonPrefix, inspectCursor } from '@/lib/client/text';
import { tokenAtPoint } from '@/lib/client/textarea-hit';
import { tokenAt } from '@/lib/cursor';
import type { Anchor, InspectOptions } from '@/lib/client/use-inspect';
import type { SessionView } from '@/lib/client/store';

interface Popup {
    items: string[];
    /** Range of the input the chosen item replaces (UTF-16 indices). */
    start: number;
    end: number;
    selected: number;
    /** Opened by typing rather than by Tab: it must not steal Enter. */
    auto: boolean;
    /** The user moved through the list with the arrow keys. */
    navigated: boolean;
}

interface Props {
    session: SessionView;
    canRun: boolean;
    value: string;
    onChange: (value: string) => void;
    onRun: () => void;
    onInterrupt: () => void;
    onClear: () => void;
    onInspect: (title: string, code: string, cursor: number, anchor: Anchor, options?: InspectOptions) => void;
    /** The pointer or caret left the word an automatic inspect was opened for. */
    onHoverEnd: () => void;
}

const MAX_ITEMS = 200;
/** Typing pauses this long before completions are requested. */
const AUTO_COMPLETE_DELAY_MS = 180;
/** A pointer or caret has to rest on a word this long before it is inspected. */
const HOVER_INSPECT_DELAY_MS = 450;
const NAVIGATION_KEYS = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End'];

/**
 * The code input dock. Besides running code it talks to the kernel:
 *   typing          completions appear on their own (Tab accepts, Esc closes)
 *   Tab             complete the token at the caret now (or indent, at line start)
 *   Shift+Tab / Ctrl+I   inspect the symbol at the caret
 *   resting the pointer or the caret on a word inspects it automatically
 * All of them are real complete_request / inspect_request round trips.
 */
export function InputDock({ session, canRun, value, onChange, onRun, onInterrupt, onClear, onInspect, onHoverEnd }: Props) {
    const textareaRef = useRef<HTMLTextAreaElement>(null);
    const areaRef = useRef<HTMLDivElement>(null);
    const [popup, setPopup] = useState<Popup | null>(null);
    const [hint, setHint] = useState('');
    const requestSeq = useRef(0);
    const pendingCaret = useRef<number | null>(null);
    const hintTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
    const autoTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
    const hoverTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
    const hoveredToken = useRef<{ token: string; start: number } | null>(null);
    const lastMove = useRef(0);

    // `sticky` hints stay until replaced (used while waiting for a busy kernel).
    const showHint = useCallback((text: string, sticky = false) => {
        setHint(text);
        clearTimeout(hintTimer.current);
        if (!sticky) hintTimer.current = setTimeout(() => setHint(''), 3000);
    }, []);

    useEffect(() => () => {
        clearTimeout(hintTimer.current);
        clearTimeout(autoTimer.current);
        clearTimeout(hoverTimer.current);
    }, []);

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

    const cancelHover = useCallback(() => {
        clearTimeout(hoverTimer.current);
        hoveredToken.current = null;
    }, []);

    const edit = (start: number, end: number, replacement: string) => {
        onChange(value.slice(0, start) + replacement + value.slice(end));
        pendingCaret.current = start + replacement.length;
    };

    const accept = (p: Popup, index = p.selected) => {
        edit(p.start, p.end, p.items[index]);
        setPopup(null);
    };

    /**
     * Asks the kernel for completions. `auto` (as-you-type) requests never wait behind
     * a running cell, never edit the text and stay silent when there is nothing to show.
     */
    const requestCompletion = async (opts: { auto?: boolean; code?: string; cursor?: number } = {}) => {
        const ta = textareaRef.current;
        if (!ta) return;
        const auto = opts.auto ?? false;
        const cursor = opts.cursor ?? ta.selectionStart;
        const code = opts.code ?? value;
        const mine = ++requestSeq.current;

        if (!auto) showHint(session.running ? 'Kernel is running code — completing when it finishes…' : 'Completing…', session.running);
        let result;
        try {
            result = await api.complete(session.id, code, cursor, undefined, auto);
        } catch (error) {
            if (!auto && mine === requestSeq.current) showHint(`Completion failed: ${(error as Error).message}`);
            return;
        }
        // The user typed on (or asked again) while this was in flight.
        if (mine !== requestSeq.current || textareaRef.current?.value !== code) return;

        if (auto && (result.busy || !result.ok)) return;
        if (result.busy) return showHint('The kernel did not answer in time — try again.');
        if (!result.ok) return showHint(result.error ?? 'Completion failed.');
        const items = (result.matches ?? []).slice(0, MAX_ITEMS);
        if (items.length === 0) {
            if (!auto) showHint('No completions.');
            return;
        }
        if (!auto) setHint('');

        // The kernel says which range to replace; fall back to the token at
        // the caret if it sent something nonsensical.
        let start = result.cursorStart ?? cursor;
        let end = result.cursorEnd ?? cursor;
        if (start < 0 || end > code.length || start > end) {
            const t = tokenAt(code, cursor);
            start = t.start;
            end = t.end;
        }

        if (auto) {
            // Nothing to add when the only candidate is what is already typed.
            const typed = code.slice(start, end);
            if (items.every((item) => item === typed)) return;
            setPopup({ items, start, end, selected: 0, auto: true, navigated: false });
            return;
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
        setPopup({ items, start, end, selected: 0, auto: false, navigated: false });
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
        }, { kernelBusy: session.running });
    };

    // -- automatic inspect: the pointer or the caret resting on a word --------

    const scheduleHoverInspect = (token: string, start: number, anchor: Anchor) => {
        clearTimeout(hoverTimer.current);
        hoveredToken.current = { token, start };
        hoverTimer.current = setTimeout(() => {
            const name = inspectableToken(token);
            onInspect(name, value, start + name.length, anchor, { hover: true });
        }, HOVER_INSPECT_DELAY_MS);
    };

    const onMouseMove = (e: React.MouseEvent<HTMLTextAreaElement>) => {
        if (!canRun || session.running) return;
        const now = performance.now();
        if (now - lastMove.current < 60) return;
        lastMove.current = now;

        const hit = tokenAtPoint(e.currentTarget, e.clientX, e.clientY);
        if (!hit || !isInspectableToken(hit.token)) {
            if (hoveredToken.current) {
                cancelHover();
                onHoverEnd();
            }
            return;
        }
        const current = hoveredToken.current;
        if (current && current.start === hit.start && current.token === hit.token) return;
        scheduleHoverInspect(hit.token, hit.start, { kind: 'point', x: hit.rect.left, y: hit.rect.bottom });
    };

    const onMouseLeave = () => {
        if (hoveredToken.current) {
            cancelHover();
            onHoverEnd();
        }
    };

    // The caret coming to rest on a word after moving it (arrows, Home/End,
    // a click) -- not while typing, where completion is what helps.
    const scheduleCaretInspect = () => {
        const ta = textareaRef.current;
        if (!ta || !areaRef.current || !canRun || session.running) return;
        const t = tokenAt(value, ta.selectionStart);
        if (!isInspectableToken(t.token) || ta.selectionStart !== ta.selectionEnd) {
            if (hoveredToken.current) {
                cancelHover();
                onHoverEnd();
            }
            return;
        }
        const rect = areaRef.current.getBoundingClientRect();
        scheduleHoverInspect(t.token, t.start, { kind: 'above', left: rect.left, bottom: window.innerHeight - rect.top + 8 });
    };

    const indent = () => (session.kernelType === 'python' ? '    ' : '  ');

    const onKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
        const plainTab = e.key === 'Tab' && !e.shiftKey && !e.ctrlKey && !e.altKey && !e.metaKey;
        // Any key ends a pending automatic inspect.
        if (hoveredToken.current) {
            cancelHover();
            onHoverEnd();
        }

        if (popup) {
            if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                e.preventDefault();
                const step = e.key === 'ArrowDown' ? 1 : -1;
                setPopup({ ...popup, navigated: true, selected: (popup.selected + step + popup.items.length) % popup.items.length });
                return;
            }
            const enter = e.key === 'Enter' && !e.shiftKey && !e.ctrlKey && !e.metaKey;
            // An as-you-type popup only takes Enter once the user picked something with
            // the arrows; otherwise Enter stays a newline.
            if ((enter && (!popup.auto || popup.navigated)) || plainTab) {
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

    const onKeyUp = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
        if (NAVIGATION_KEYS.includes(e.key) && !popup) scheduleCaretInspect();
    };

    const onInput = (e: React.ChangeEvent<HTMLTextAreaElement>) => {
        const ta = e.target;
        const next = ta.value;
        const caret = ta.selectionStart;
        const inputType = (e.nativeEvent as InputEvent).inputType ?? '';

        setPopup(null);
        setHint('');
        requestSeq.current++;
        cancelHover();
        onHoverEnd();
        onChange(next);

        clearTimeout(autoTimer.current);
        // Deleting is editing, not asking for help; only typed/pasted text triggers completion.
        if (canRun && !session.running && !inputType.startsWith('delete') && shouldAutoComplete(next, caret)) {
            autoTimer.current = setTimeout(() => void requestCompletion({ auto: true, code: next, cursor: caret }), AUTO_COMPLETE_DELAY_MS);
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
                            <div className="completion-footer">↑↓ choose · Tab accept{popup.auto && !popup.navigated ? '' : ' · Enter accept'} · Esc close</div>
                        </div>
                    )}
                    <textarea
                        ref={textareaRef}
                        className="repl-input"
                        value={value}
                        placeholder="Enter code for the active kernel..."
                        spellCheck={false}
                        onChange={onInput}
                        onKeyDown={onKeyDown}
                        onKeyUp={onKeyUp}
                        onMouseMove={onMouseMove}
                        onMouseLeave={onMouseLeave}
                        onClick={() => { if (!popup) scheduleCaretInspect(); }}
                        onBlur={() => { setPopup(null); cancelHover(); }}
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
