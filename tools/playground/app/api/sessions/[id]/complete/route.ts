import { toCodePointIndex, toUtf16Index } from '@/lib/cursor';
import { errorMessage, json, readJson, withEntry, type RouteContext } from '@/lib/server/http';
import { KERNEL_BUSY } from '@/lib/server/registry';
import type { CompletionResult } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// While a cell runs, Python answers a completion at once and R through its
// helper process (Session.complete()); Stata answers only between executions,
// so one asked mid-run queues behind the run and is answered when it finishes:
// wait for it (the client says so and drops the answer if the user has typed
// on meanwhile) instead of failing. An idle kernel answers in milliseconds, so
// a short cap is enough there.
const IDLE_TIMEOUT_MS = 2000;
const BUSY_TIMEOUT_MS = 30 * 60 * 1000;

// POST /api/sessions/:id/complete  { code, cursorPos }
export function POST(req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const { code = '', cursorPos = code.length, noWait = false } = await readJson<{ code?: string; cursorPos?: number; noWait?: boolean }>(req);

        if (entry.status !== 'ready') {
            return json({ ok: false, error: `session is ${entry.status}` } satisfies CompletionResult);
        }

        // Automatic requests (as-you-type completion, hover inspect) must never
        // sit behind a running cell (waitForCell: false): the session answers
        // at once -- from Python itself, or from R's helper process -- or says
        // it is busy, and the UI stays quiet.
        try {
            const reply = await entry.session.complete(code, toCodePointIndex(code, cursorPos), {
                waitForCell: !noWait,
                timeout: entry.running > 0 ? BUSY_TIMEOUT_MS : IDLE_TIMEOUT_MS
            });
            if (reply.metadata?.[KERNEL_BUSY]) {
                return json({ ok: false, busy: true } satisfies CompletionResult);
            }

            const result: CompletionResult = {
                ok: true,
                matches: reply.matches ?? [],
                cursorStart: toUtf16Index(code, reply.cursor_start ?? cursorPos),
                cursorEnd: toUtf16Index(code, reply.cursor_end ?? cursorPos)
            };
            return json(result);
        } catch (error) {
            const message = errorMessage(error);
            return json({ ok: false, busy: /timed out/i.test(message), error: message } satisfies CompletionResult);
        }
    });
}
