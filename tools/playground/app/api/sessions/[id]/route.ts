import { getRegistry } from '@/lib/server/registry';
import { json, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// DELETE /api/sessions/:id -- forgets the session on the server too (so a
// refreshed page does not resurrect it), stopping its kernel first if it is
// still alive so removing one never leaves an orphaned process behind.
export function DELETE(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry, id) => {
        if (entry.status !== 'stopped') {
            await entry.session.stop().catch(() => {});
            entry.status = 'stopped';
        }
        (await getRegistry()).sessions.delete(id);
        return json({ ok: true });
    });
}
