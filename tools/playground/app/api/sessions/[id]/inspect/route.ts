import { toCodePointIndex } from '@/lib/cursor';
import { errorMessage, json, readJson, withEntry, type RouteContext } from '@/lib/server/http';
import type { InspectResult } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// See the complete route: a busy kernel answers when its run finishes, so
// wait for it instead of failing; an idle one answers at once.
const IDLE_TIMEOUT_MS = 2000;
const BUSY_TIMEOUT_MS = 30 * 60 * 1000;

// The mime bundle's text/plain, which may be a string or a list of lines.
function plainText(data: Record<string, unknown> | undefined): string {
    const value = data?.['text/plain'];
    return Array.isArray(value) ? value.join('\n') : typeof value === 'string' ? value : '';
}

// POST /api/sessions/:id/inspect  { code, cursorPos }
export function POST(req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const { code = '', cursorPos = code.length, noWait = false } = await readJson<{ code?: string; cursorPos?: number; noWait?: boolean }>(req);

        if (entry.status !== 'ready') {
            return json({ ok: false, error: `session is ${entry.status}` } satisfies InspectResult);
        }

        // Automatic requests (as-you-type completion, hover inspect) must never
        // sit behind a running cell: answer "busy" at once and let the UI stay quiet.
        if (noWait && entry.running > 0) {
            return json({ ok: false, busy: true } satisfies InspectResult);
        }

        try {
            const reply = await entry.session.request<{ found?: boolean; data?: Record<string, unknown> }>(
                'inspect_request',
                { code, cursor_pos: toCodePointIndex(code, cursorPos), detail_level: 0 },
                { timeout: entry.running > 0 ? BUSY_TIMEOUT_MS : IDLE_TIMEOUT_MS }
            );
            const text = plainText(reply.data);
            return json({ ok: true, found: Boolean(reply.found) && text !== '', text } satisfies InspectResult);
        } catch (error) {
            const message = errorMessage(error);
            return json({ ok: false, busy: /timed out/i.test(message), error: message } satisfies InspectResult);
        }
    });
}
