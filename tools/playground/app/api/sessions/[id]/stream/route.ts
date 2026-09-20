import { json, type RouteContext } from '@/lib/server/http';
import { getEntry } from '@/lib/server/registry';
import type { StreamEvent } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// GET /api/sessions/:id/stream -- Server-Sent Events: every iopub message
// the session produces, plus lifecycle events (exit/stopped/restarted).
export async function GET(req: Request, ctx: RouteContext) {
    const { id } = await ctx.params;
    const entry = await getEntry(id);
    if (!entry) return json({ error: 'unknown session' }, 404);

    const encoder = new TextEncoder();
    let cleanup = () => {};

    const stream = new ReadableStream<Uint8Array>({
        start(controller) {
            const write = (text: string) => {
                try {
                    controller.enqueue(encoder.encode(text));
                } catch {
                    cleanup();
                }
            };
            const send = (payload: StreamEvent) => write(`data: ${JSON.stringify(payload)}\n\n`);

            send({ event: 'connected', status: entry.status });
            entry.clients.add(send);
            const heartbeat = setInterval(() => write(': ping\n\n'), 15000);

            cleanup = () => {
                clearInterval(heartbeat);
                entry.clients.delete(send);
                try {
                    controller.close();
                } catch {
                    // already closed
                }
                cleanup = () => {};
            };
            req.signal.addEventListener('abort', () => cleanup());
        },
        cancel() {
            cleanup();
        }
    });

    return new Response(stream, {
        headers: {
            'Content-Type': 'text/event-stream',
            'Cache-Control': 'no-cache, no-transform',
            Connection: 'keep-alive',
            'X-Accel-Buffering': 'no'
        }
    });
}
