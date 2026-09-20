// How the kernel's heartbeat is shown. Pure, so it can be unit-tested.

export interface HeartbeatView {
    /** Round trip of the latest answered ping; null before the first one. */
    rttMs: number | null;
    /** How long ago that answer arrived. */
    sinceLastPongMs: number | null;
    /** Pings in a row that went unanswered. */
    misses: number;
}

export type HeartbeatTone = 'ok' | 'warn' | 'bad' | 'idle';

/** No answer for this long counts as a lost heartbeat even before a ping is formally missed. */
export const STALE_AFTER_MS = 3000;

export function describeHeartbeat(hb: HeartbeatView | null, status: string): { text: string; tone: HeartbeatTone } {
    if (status === 'stopped' || status === 'starting') return { text: '\u2014', tone: 'idle' };
    if (status === 'crashed') return { text: 'lost', tone: 'bad' };
    if (!hb) return { text: '\u2014', tone: 'idle' };
    if (hb.rttMs === null) return { text: hb.misses > 0 ? 'no reply' : '\u2026', tone: hb.misses > 0 ? 'bad' : 'idle' };
    if (hb.misses > 0 || (hb.sinceLastPongMs ?? 0) > STALE_AFTER_MS) return { text: 'no reply', tone: 'bad' };

    const text = hb.rttMs < 10 ? `${hb.rttMs.toFixed(1)}ms` : `${Math.round(hb.rttMs)}ms`;
    return { text, tone: hb.rttMs < 100 ? 'ok' : hb.rttMs < 1000 ? 'warn' : 'bad' };
}
