import { json, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// POST /api/sessions/:id/stop -- graceful: a real shutdown_request, then a
// force-kill by the supervisor only if the kernel does not exit.
export function POST(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        await entry.session.stop();
        return json({ ok: true });
    });
}
