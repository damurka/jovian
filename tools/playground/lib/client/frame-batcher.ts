// Collects items and hands them over in one batch per display frame.
//
// A kernel can send thousands of messages a second (a loop alternating
// cat() and message() makes two per iteration). Applying each one as its own
// React state update means one reducer pass and one render per message --
// far more renders than the screen can show. Batched, the page renders at
// most once per frame however fast output arrives, and nothing is lost or
// reordered.
//
// requestAnimationFrame does not run in a background tab, so a timer flushes
// too (at most `maxDelayMs` later): output keeps being applied, just not
// painted, and a tab brought back shows everything at once.

export interface FrameBatcher<T> {
    push(item: T): void;
    /** Hands over what is waiting now (before something that must come after it). */
    flush(): void;
    /** Drops what is waiting and cancels the scheduled flush. */
    dispose(): void;
}

export interface Scheduler {
    requestFrame(callback: () => void): unknown;
    cancelFrame(handle: unknown): void;
    setTimer(callback: () => void, ms: number): unknown;
    clearTimer(handle: unknown): void;
}

const browserScheduler = (): Scheduler => ({
    requestFrame: (cb) => (typeof requestAnimationFrame === 'function' ? requestAnimationFrame(cb) : setTimeout(cb, 16)),
    cancelFrame: (h) => (typeof cancelAnimationFrame === 'function' ? cancelAnimationFrame(h as number) : clearTimeout(h as ReturnType<typeof setTimeout>)),
    setTimer: (cb, ms) => setTimeout(cb, ms),
    clearTimer: (h) => clearTimeout(h as ReturnType<typeof setTimeout>)
});

export function createFrameBatcher<T>(
    deliver: (items: T[]) => void,
    { maxDelayMs = 100, scheduler = browserScheduler() }: { maxDelayMs?: number; scheduler?: Scheduler } = {}
): FrameBatcher<T> {
    let pending: T[] = [];
    let frame: unknown;
    let timer: unknown;

    const cancel = () => {
        if (frame !== undefined) scheduler.cancelFrame(frame);
        if (timer !== undefined) scheduler.clearTimer(timer);
        frame = undefined;
        timer = undefined;
    };

    const flush = () => {
        cancel();
        if (pending.length === 0) return;
        const items = pending;
        pending = [];
        deliver(items);
    };

    return {
        push(item) {
            pending.push(item);
            if (frame === undefined) frame = scheduler.requestFrame(flush);
            if (timer === undefined) timer = scheduler.setTimer(flush, maxDelayMs);
        },
        flush,
        dispose() {
            cancel();
            pending = [];
        }
    };
}
