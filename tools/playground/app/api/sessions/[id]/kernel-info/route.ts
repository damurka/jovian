import { errorMessage, json, withEntry, type RouteContext } from '@/lib/server/http';
import type { KernelInfo } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// GET /api/sessions/:id/kernel-info -- the kernel's own answer to a real
// kernel_info_request: what it is and which language version it runs.
export function GET(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        try {
            const info = await entry.session.kernelInfo();
            const body: KernelInfo = {
                language: info.language_info?.name,
                version: info.language_info?.version,
                implementation: info.implementation,
                protocolVersion: info.protocol_version,
                banner: info.banner
            };
            return json(body);
        } catch (error) {
            return json({ error: errorMessage(error) }, 502);
        }
    });
}
