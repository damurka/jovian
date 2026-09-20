import { errorMessage, json, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// GET /api/sessions/:id/info -- proxies the supervisor's own
// GET {httpBase}/sessions/{sessionId}, the only place the kernel process's
// pid and memory usage live (native/src/themisto/session_registry.cpp's
// sessionToJson()).
export function GET(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        try {
            const { httpBase, sessionId } = entry.session.info;
            const upstream = await fetch(`${httpBase}/sessions/${sessionId}`);
            return json(await upstream.json(), upstream.status);
        } catch (error) {
            return json({ error: errorMessage(error) }, 502);
        }
    });
}
