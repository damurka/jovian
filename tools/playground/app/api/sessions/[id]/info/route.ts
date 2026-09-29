import { errorMessage, json, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// GET /api/sessions/:id/info -- the supervisor's view of the session
// (Session.status(): pid, memory, heartbeat -- native/src/themisto/
// session_registry.cpp's sessionToJson()).
export function GET(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        try {
            return json(await entry.session.status());
        } catch (error) {
            return json({ error: errorMessage(error) }, 502);
        }
    });
}
