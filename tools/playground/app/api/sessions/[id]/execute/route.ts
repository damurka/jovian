import { errorMessage, json, readJson, withEntry, type RouteContext } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// POST /api/sessions/:id/execute  { code, timeout?, allowStdin? }
export function POST(req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const body = await readJson<{ code?: string; timeout?: number; allowStdin?: boolean }>(req);
        entry.running += 1;
        try {
            const result = await entry.session.execute(body.code ?? '', {
                // No timeout unless the client asks for one: an interactive
                // session runs until it finishes or the user presses
                // Interrupt (a client-side timeout would leave the kernel
                // busy with code nobody is waiting for).
                timeout: typeof body.timeout === 'number' ? body.timeout : 0,
                // The UI can always answer an input_request (see the
                // 'input' route), so every execution opts into stdin by
                // default instead of each preset having to ask.
                allowStdin: body.allowStdin !== false
            });
            return json({
                ok: true,
                success: result.success,
                executionCount: result.executionCount,
                error: result.error ? errorMessage(result.error) : undefined
            });
        } catch (error) {
            return json({ ok: false, success: false, error: errorMessage(error) });
        } finally {
            entry.running -= 1;
        }
    });
}
