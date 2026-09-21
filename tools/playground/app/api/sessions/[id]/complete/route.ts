import { toCodePointIndex, toUtf16Index } from '@/lib/cursor';
import { errorMessage, json, readJson, withEntry, type RouteContext } from '@/lib/server/http';
import type { CompletionResult } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// A kernel only answers requests between executions (it handles them on the
// one thread that runs code), so a completion asked mid-run queues behind the
// run and is answered when it finishes: wait for it (the client says so and
// drops the answer if the user has typed on meanwhile) instead of failing.
// An idle kernel answers in milliseconds, so a short cap is enough there.
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
        // sit behind a running cell: answer "busy" at once and let the UI stay quiet.
        if (noWait && entry.running > 0) {
            return json({ ok: false, busy: true } satisfies CompletionResult);
        }

        try {
            const reply = await entry.session.request<{
                matches?: string[]; cursor_start?: number; cursor_end?: number;
            }>('complete_request', { code, cursor_pos: toCodePointIndex(code, cursorPos) }, { timeout: entry.running > 0 ? BUSY_TIMEOUT_MS : IDLE_TIMEOUT_MS });

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
