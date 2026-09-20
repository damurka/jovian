import { createPlaygroundSession, getRegistry, summarize, type CreateOptions } from '@/lib/server/registry';
import { errorMessage, json, readJson } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// GET /api/sessions -- every session this server holds (a refreshed page
// rebuilds its sidebar from this).
export async function GET() {
    const { sessions } = await getRegistry();
    return json({ sessions: [...sessions.values()].map(summarize) });
}

// POST /api/sessions -- spawns a real kernel process.
export async function POST(req: Request) {
    const body = await readJson<CreateOptions>(req);
    try {
        const entry = await createPlaygroundSession(body);
        return json({ id: entry.id }, 201);
    } catch (error) {
        return json({ error: errorMessage(error) }, 500);
    }
}
