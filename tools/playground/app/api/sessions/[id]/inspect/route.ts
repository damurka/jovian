import { toCodePointIndex } from '@/lib/cursor';
import { errorMessage, json, readJson, withEntry, type RouteContext } from '@/lib/server/http';
import type { InspectResult } from '@/lib/types';
import { KERNEL_BUSY } from '@/lib/server/registry';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// See the complete route: Python, R's helper (for a package's function) or an
// idle kernel answer at once; otherwise the answer comes when the run
// finishes, so wait for it instead of failing.
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

        // Automatic requests (hover inspect) must never sit behind a running
        // cell (waitForCell: false): the session answers at once -- from
        // Python itself, or from R's helper process for a package's
        // function -- or says it is busy, and the UI stays quiet.
        try {
            const reply = await entry.session.inspect(code, toCodePointIndex(code, cursorPos), 0, {
                waitForCell: !noWait,
                timeout: entry.running > 0 ? BUSY_TIMEOUT_MS : IDLE_TIMEOUT_MS
            });
            if (reply.metadata?.[KERNEL_BUSY]) {
                return json({ ok: false, busy: true } satisfies InspectResult);
            }
            const text = plainText(reply.data);
            return json({ ok: true, found: Boolean(reply.found) && text !== '', text } satisfies InspectResult);
        } catch (error) {
            const message = errorMessage(error);
            return json({ ok: false, busy: /timed out/i.test(message), error: message } satisfies InspectResult);
        }
    });
}
