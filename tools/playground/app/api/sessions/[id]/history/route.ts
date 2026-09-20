import { toWire } from '@/lib/server/registry';
import { json, withEntry, type RouteContext } from '@/lib/server/http';
import type { HistoryRecord } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// GET /api/sessions/:id/history -- Session.getHistory(): every execution's
// code plus the iopub messages it produced, for a refreshed page to replay
// through the same reducer live messages go through.
export function GET(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, (entry) => {
        const history: HistoryRecord[] = entry.session.getHistory().map((h) => ({
            code: h.code,
            executionCount: h.executionCount,
            time: h.time,
            messages: h.messages.map((m) => toWire(m))
        }));
        return json({ history });
    });
}
