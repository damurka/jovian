import { NextResponse, type NextRequest } from 'next/server';

// The API runs code in real kernels, so a page on some other origin must not
// be able to drive it through the user's browser (CSRF against localhost):
// state-changing requests are only accepted from this app's own origin.
export function proxy(req: NextRequest) {
    if (req.method !== 'GET' && req.method !== 'HEAD') {
        const origin = req.headers.get('origin');
        if (origin) {
            let originHost = '';
            try {
                originHost = new URL(origin).host;
            } catch {
                // fall through: an unparsable origin never matches
            }
            if (originHost !== req.headers.get('host')) {
                return NextResponse.json({ error: 'cross-origin request rejected' }, { status: 403 });
            }
        }
    }
    return NextResponse.next();
}

export const config = { matcher: '/api/:path*' };
