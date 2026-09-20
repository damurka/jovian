import { json, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// POST /api/sessions/:id/interrupt -- a real interrupt_request on the
// control channel. `acknowledged` is whether the kernel answered with an
// interrupt_reply; the short timeout keeps the UI's button from hanging.
export function POST(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const acknowledged = await entry.session.interrupt({ timeout: 1500 });
        return json({ ok: true, acknowledged });
    });
}
