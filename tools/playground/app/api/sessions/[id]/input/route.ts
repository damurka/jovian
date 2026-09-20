import { json, readJson, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// POST /api/sessions/:id/input  { value } -- answers a pending input_request
// (Session.sendInputReply()). Fire-and-forget: what matters is what the
// blocked execute() call eventually resolves with, not a reply to this.
export function POST(req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const body = await readJson<{ value?: unknown }>(req);
        entry.session.sendInputReply(String(body.value ?? ''));
        return json({ ok: true });
    });
}
