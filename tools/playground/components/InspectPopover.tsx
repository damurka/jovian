'use client';

import { useEffect, useRef } from 'react';
import type { InspectState } from '@/lib/client/use-inspect';

/** Floating documentation card for the last inspect request. Esc or a click elsewhere closes it. */
export function InspectPopover({ state, onClose, onPointerEnter, onPointerLeave }: {
    state: InspectState | null;
    onClose: () => void;
    /** For popovers opened by hovering: keep it while the pointer is on it, close it when it leaves. */
    onPointerEnter?: () => void;
    onPointerLeave?: () => void;
}) {
    const ref = useRef<HTMLDivElement>(null);

    useEffect(() => {
        if (!state) return;
        const onKey = (e: KeyboardEvent) => {
            if (e.key === 'Escape') onClose();
        };
        const onDown = (e: MouseEvent) => {
            if (ref.current && !ref.current.contains(e.target as Node)) onClose();
        };
        window.addEventListener('keydown', onKey);
        // Deferred a tick so the very click that opened it (a double-click's
        // second mouseup) doesn't immediately dismiss it.
        const timer = setTimeout(() => window.addEventListener('mousedown', onDown), 0);
        return () => {
            clearTimeout(timer);
            window.removeEventListener('keydown', onKey);
            window.removeEventListener('mousedown', onDown);
        };
    }, [state, onClose]);

    if (!state) return null;

    const style: React.CSSProperties = {};
    const width = 560;
    if (state.anchor.kind === 'above') {
        style.left = Math.max(12, Math.min(state.anchor.left, window.innerWidth - width - 12));
        style.bottom = state.anchor.bottom;
    } else {
        style.left = Math.max(12, Math.min(state.anchor.x, window.innerWidth - width - 12));
        // Below the pointer unless that would run off the bottom.
        const below = state.anchor.y + 14;
        if (below + 220 > window.innerHeight) style.bottom = Math.max(12, window.innerHeight - state.anchor.y + 14);
        else style.top = below;
    }

    return (
        <div className="inspect-popover" style={style} ref={ref} onMouseEnter={onPointerEnter} onMouseLeave={onPointerLeave} role="dialog" aria-label={`Documentation for ${state.title}`}>
            <div className="inspect-popover-header">
                <span>Inspect <code>{state.title}</code></span>
                <button onClick={onClose} aria-label="Close">&#10005;</button>
            </div>
            <div className={'inspect-popover-body' + (state.phase === 'found' ? '' : ' muted')}>
                {state.phase === 'loading' ? (state.text || 'Asking the kernel…') : state.text}
            </div>
        </div>
    );
}
