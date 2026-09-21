'use client';

import { useCallback, useRef, useState } from 'react';
import { api } from './api.ts';
import { cleanHelpText } from './text.ts';

export type Anchor =
    | { kind: 'point'; x: number; y: number }
    /** Sits above an element (the input dock), left-aligned to it. */
    | { kind: 'above'; left: number; bottom: number };

export interface InspectState {
    title: string;
    anchor: Anchor;
    phase: 'loading' | 'found' | 'none' | 'busy' | 'error';
    text: string;
    /**
     * Opened by resting the pointer or caret on a word, not by asking (Shift+Tab,
     * double-click): it closes again when that rest ends, and never shows
     * "loading", "not found" or errors -- it only appears if there is something to show.
     */
    hover?: boolean;
}

export interface InspectOptions {
    /** The kernel is running code, so an explicit request will wait for it. */
    kernelBusy?: boolean;
    hover?: boolean;
}

/**
 * Runs inspect requests for one session and keeps the latest result. Each
 * request gets a sequence number so a slow answer for something the user
 * has already moved on from never overwrites a newer one.
 */
export function useInspect(sessionId: string | null) {
    const [state, setState] = useState<InspectState | null>(null);
    const stateRef = useRef<InspectState | null>(null);
    const seq = useRef(0);
    const controller = useRef<AbortController | null>(null);

    const publish = useCallback((next: InspectState | null) => {
        stateRef.current = next;
        setState(next);
    }, []);

    const close = useCallback(() => {
        seq.current++;
        controller.current?.abort();
        publish(null);
    }, [publish]);

    /** Closes a popover that was only opened by hovering / resting; leaves an explicit one alone. */
    const closeHover = useCallback(() => {
        if (stateRef.current?.hover) close();
    }, [close]);

    const inspect = useCallback(async (
        title: string, code: string, cursorPos: number, anchor: Anchor, options: InspectOptions = {}
    ) => {
        if (!sessionId) return;
        const { kernelBusy = false, hover = false } = options;
        const mine = ++seq.current;
        controller.current?.abort();
        const abort = new AbortController();
        controller.current = abort;

        if (!hover) {
            // A kernel that is running code answers after it finishes -- say so
            // instead of looking hung.
            publish({
                title, anchor, phase: 'loading',
                text: kernelBusy ? 'The kernel is running code. This will appear as soon as it finishes (or press Interrupt).' : ''
            });
        }

        try {
            const result = await api.inspect(sessionId, code, cursorPos, abort.signal, hover);
            if (mine !== seq.current) return;
            if (result.ok && result.found) {
                publish({ title, anchor, phase: 'found', text: cleanHelpText(result.text ?? ''), hover });
            } else if (hover) {
                closeHover();
            } else if (result.busy) {
                publish({ title, anchor, phase: 'busy', text: 'The kernel did not answer in time. Try again in a moment.' });
            } else if (!result.ok) {
                publish({ title, anchor, phase: 'error', text: result.error ?? 'Inspect failed.' });
            } else {
                publish({ title, anchor, phase: 'none', text: `No documentation found for “${title}”.` });
            }
        } catch (error) {
            if (mine !== seq.current || (error as Error).name === 'AbortError') return;
            if (hover) closeHover();
            else publish({ title, anchor, phase: 'error', text: String((error as Error).message ?? error) });
        }
    }, [sessionId, publish, closeHover]);

    return { state, inspect, close, closeHover };
}
