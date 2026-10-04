import path from 'node:path';
import { broadcast, getRegistry, type Entry } from '@/lib/server/registry';
import { errorMessage, json, readJson, withEntry, type RouteContext } from '@/lib/server/http';
import type { PackagesInfo, PackagesInstallResult, WhenInUse } from '@/lib/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

const WHEN_IN_USE: WhenInUse[] = ['wait', 'defer', 'proceed'];

// Where a session's packages are installed: an R session's first library (its
// own when it was given library paths, else R's), a Python session's virtual
// environment.
async function target(entry: Entry): Promise<{ libraries?: string[]; venv?: string; coordinated: boolean; reason?: string }> {
    const { jovian } = await getRegistry();
    if (entry.kernelType === 'r') {
        const own = entry.config.rLibs?.split(path.delimiter).filter((library) => library.length > 0) ?? [];
        if (own.length > 0) return { libraries: own, coordinated: true };
        return { libraries: await jovian.readRLibraries(entry.config.rHome ?? ''), coordinated: false };
    }
    if (entry.kernelType === 'python') {
        return entry.config.venvPath
            ? { venv: entry.config.venvPath, coordinated: true }
            : { coordinated: false, reason: 'This Python session has no virtual environment. Create a session with a virtual environment to install packages into it.' };
    }
    return { coordinated: false, reason: 'Packages are installed for R and Python sessions.' };
}

// GET /api/sessions/:id/packages -- where installs go, and what the session
// has loaded now.
export function GET(_req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const where = await target(entry);
        // undefined while the kernel runs code (it may load anything): said
        // at once, and asking never interrupts the cell
        const loaded = entry.kernelType === 'r' && entry.status === 'ready'
            ? await entry.session.loadedRPackages().catch(() => undefined)
            : undefined;
        const info: PackagesInfo = {
            supported: Boolean(where.libraries?.length || where.venv),
            reason: where.reason,
            library: where.libraries?.[0] ?? where.venv,
            coordinated: where.coordinated,
            loaded: loaded ?? null
        };
        return json(info);
    });
}

// POST /api/sessions/:id/packages  { name, update?, whenInUse? } -- installs
// through the manager's installer (ensureRPackage / ensurePythonPackages);
// its progress lines arrive on the session's event stream.
export function POST(req: Request, ctx: RouteContext) {
    return withEntry(ctx, async (entry) => {
        const body = await readJson<{ name?: string; update?: boolean; whenInUse?: WhenInUse }>(req);
        const name = (body.name ?? '').trim();
        if (!name) return json({ ok: false, error: 'Name a package.' } satisfies PackagesInstallResult);
        const whenInUse = body.whenInUse && WHEN_IN_USE.includes(body.whenInUse) ? body.whenInUse : 'wait';
        const { manager, jovian, sessions } = await getRegistry();
        const where = await target(entry);
        const onOutput = (line: string) => broadcast(entry, { event: 'packages', line });
        const started = Date.now();
        try {
            let summary: string;
            if (where.libraries?.length) {
                const result = await manager.ensureRPackage(
                    { name, update: Boolean(body.update), optional: false },
                    { rHome: entry.config.rHome ?? '', libraries: where.libraries, whenInUse, onOutput }
                );
                summary = result.installed.length > 0
                    ? `Installed ${result.installed.join(', ')}: ${name} ${result.previousVersion ? `${result.previousVersion} -> ` : ''}${result.version}`
                    : `Nothing to do: ${name} ${result.version} is installed`;
            } else if (where.venv) {
                const result = await manager.ensurePythonPackages(
                    where.venv,
                    // a bare name can be updated; anything else ("six>=1.17") is a requirement as written
                    /^[A-Za-z0-9._-]+$/.test(name) ? { name, update: Boolean(body.update) } : { requirements: [name] },
                    { whenInUse, onOutput }
                );
                summary = result.changed ? `Installed ${name}${result.version ? ` ${result.version}` : ''}` : `Nothing to do: ${name} is installed`;
            } else {
                return json({ ok: false, error: where.reason ?? 'Nothing to install into.' } satisfies PackagesInstallResult);
            }
            return json({ ok: true, summary, ms: Date.now() - started } satisfies PackagesInstallResult);
        } catch (error) {
            // 'defer': nothing was installed, and the error names the sessions in the way
            const ids = (error as Error)?.name === jovian.PACKAGES_IN_USE ? (error as { sessions?: string[] }).sessions ?? [] : undefined;
            const inUseBy = ids?.map((id) => [...sessions.values()].find((other) => other.session.info.sessionId === id)?.name ?? id);
            return json({ ok: false, error: errorMessage(error), inUseBy, ms: Date.now() - started } satisfies PackagesInstallResult);
        }
    });
}
