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
}

/**
 * Runs inspect requests for one session and keeps the latest result. Each
 * request gets a sequence number so a slow answer for something the user
 * has already moved on from never overwrites a newer one.
 */
export function useInspect(sessionId: string | null) {
    const [state, setState] = useState<InspectState | null>(null);
    const seq = useRef(0);
    const controller = useRef<AbortController | null>(null);

    const close = useCallback(() => {
        seq.current++;
        controller.current?.abort();
        setState(null);
    }, []);

    const inspect = useCallback(async (title: string, code: string, cursorPos: number, anchor: Anchor, kernelBusy = false) => {
        if (!sessionId) return;
        const mine = ++seq.current;
        controller.current?.abort();
        const abort = new AbortController();
        controller.current = abort;
        // A kernel that is running code answers after it finishes -- say so
        // instead of looking hung.
        setState({
            title, anchor, phase: 'loading',
            text: kernelBusy ? 'The kernel is running code. This will appear as soon as it finishes (or press Interrupt).' : ''
        });

        try {
            const result = await api.inspect(sessionId, code, cursorPos, abort.signal);
            if (mine !== seq.current) return;
            if (result.busy) {
                setState({ title, anchor, phase: 'busy', text: 'The kernel did not answer in time. Try again in a moment.' });
            } else if (!result.ok) {
                setState({ title, anchor, phase: 'error', text: result.error ?? 'Inspect failed.' });
            } else if (!result.found) {
                setState({ title, anchor, phase: 'none', text: `No documentation found for “${title}”.` });
            } else {
                setState({ title, anchor, phase: 'found', text: cleanHelpText(result.text ?? '') });
            }
        } catch (error) {
            if (mine !== seq.current || (error as Error).name === 'AbortError') return;
            setState({ title, anchor, phase: 'error', text: String((error as Error).message ?? error) });
        }
    }, [sessionId]);

    return { state, inspect, close };
}
