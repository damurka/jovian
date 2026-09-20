import { getEntry, type Entry } from './registry.ts';

export type RouteContext = { params: Promise<{ id: string }> };

export function json(body: unknown, status = 200): Response {
    return Response.json(body, { status });
}

export function errorMessage(error: unknown): string {
    return String((error as Error)?.message ?? error);
}

export async function readJson<T = Record<string, any>>(req: Request): Promise<T> {
    try {
        return (await req.json()) as T;
    } catch {
        return {} as T;
    }
}

/** Resolves the :id route param to its session, or answers 404 itself. */
export async function withEntry(
    ctx: RouteContext,
    handler: (entry: Entry, id: string) => Promise<Response> | Response
): Promise<Response> {
    const { id } = await ctx.params;
    const entry = await getEntry(id);
    if (!entry) return json({ error: 'unknown session' }, 404);
    try {
        return await handler(entry, id);
    } catch (error) {
        return json({ ok: false, error: errorMessage(error) }, 500);
    }
}
