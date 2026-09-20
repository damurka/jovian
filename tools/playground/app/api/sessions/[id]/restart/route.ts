import { errorMessage, json, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// POST /api/sessions/:id/restart -- replaces the kernel process in place,
// keeping the session id (also recovers a crashed session).
export function POST(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        try {
            await entry.session.restart();
            return json({ ok: true });
        } catch (error) {
            return json({ ok: false, error: errorMessage(error) }, 500);
        }
    });
}
