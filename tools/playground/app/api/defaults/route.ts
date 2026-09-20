import { defaultEnvironment } from '@/lib/env.mjs';
import { json } from '@/lib/server/http';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

// The R/Python installs detected on this machine, to pre-fill the
// new-session dialog (see lib/env.mjs).
export function GET() {
    return json(defaultEnvironment());
}
