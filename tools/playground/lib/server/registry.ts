// Server-side session registry, shared by every route handler.
//
// Held on globalThis, not in a module variable: `next dev` re-evaluates
// modules on hot reload (and route handlers can be bundled into separate
// chunks), which would otherwise hand each of them its own empty registry and
// orphan every running kernel.
import { statSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { randomUUID } from 'node:crypto';

import { defaultEnvironment } from '../env.mjs';
import type { Session, SessionManager } from '../../../../dist/lib/index.js';
import type { SessionConfig, SessionStatus, SessionSummary, StreamEvent, WireMessage, KernelType } from '../types.ts';

type JovianModule = typeof import('../../../../dist/lib/index.js');

export interface Entry {
    id: string;
    name: string;
    session: Session;
    kernelType: KernelType;
    status: SessionStatus;
    config: SessionConfig;
    workingDirectory?: string;
    /** Executions in flight (an execute() call that has not resolved yet). */
    running: number;
    clients: Set<(payload: StreamEvent) => void>;
}

interface Registry {
    manager: SessionManager;
    sessions: Map<string, Entry>;
}

const KEY = '__jovianPlayground';
type GlobalWithRegistry = typeof globalThis & { [KEY]?: Promise<Registry> };

// The lib is plain built ESM under <repo>/dist/lib, outside this Next
// project. Loading it with a runtime import of a file URL (and telling both
// bundlers to leave the call alone) keeps it out of the bundle entirely: no
// tracing of the native binaries next to it, and the exact same module the
// repl and the integration tests use.
function distIndexUrl(): string {
    const distDir = process.env.JOVIAN_DIST_DIR
        ?? path.resolve(/* turbopackIgnore: true */ process.cwd(), '..', '..', 'dist', 'lib');
    return pathToFileURL(path.join(distDir, 'index.js')).href;
}

async function createRegistry(): Promise<Registry> {
    let jovian: JovianModule;
    try {
        jovian = await import(/* webpackIgnore: true */ /* turbopackIgnore: true */ distIndexUrl());
    } catch (error) {
        throw new Error(
            `Could not load the jovian library from ${distIndexUrl()} -- run \`npm run build:lib\` in the repo root first. (${(error as Error).message})`
        );
    }
    return { manager: new jovian.SessionManager(), sessions: new Map() };
}

export function getRegistry(): Promise<Registry> {
    const g = globalThis as GlobalWithRegistry;
    g[KEY] ??= createRegistry();
    return g[KEY];
}

export async function getEntry(id: string): Promise<Entry | undefined> {
    return (await getRegistry()).sessions.get(id);
}

export function broadcast(entry: Entry, payload: StreamEvent): void {
    for (const send of entry.clients) {
        send(payload);
    }
}

// A message's `raw` JSON duplicates its whole content -- dropped before it
// crosses the wire to the browser (SSE or history).
export function toWire(message: { raw?: string } & WireMessage): WireMessage {
    const { raw: _raw, ...rest } = message;
    return rest;
}

export interface CreateOptions {
    name?: string;
    kernelType: KernelType;
    rHome?: string;
    rPath?: string;
    rLibs?: string;
    pythonHome?: string;
    pythonPath?: string;
    venvPath?: string;
    workingDirectory?: string;
}

const empty = (v: string | undefined) => (v && v.trim() !== '' ? v : undefined);

export async function createPlaygroundSession(options: CreateOptions): Promise<Entry> {
    const { manager, sessions } = await getRegistry();

    const workingDirectory = empty(options.workingDirectory);
    if (workingDirectory) {
        let ok = false;
        try {
            ok = statSync(workingDirectory).isDirectory();
        } catch {
            ok = false;
        }
        if (!ok) {
            throw new Error(`Working directory does not exist or is not a directory: ${workingDirectory}`);
        }
    }

    // Generated up front: the logger callback below can fire while
    // manager.createSession() is still in flight (Session's own "Connecting
    // to session ..." trace), so it needs `id` closed over before then.
    // The lib assigns its own internal session id, which its public API does
    // not expose, so this tool keys everything on a separate one.
    const id = randomUUID();
    const kernelType: KernelType = options.kernelType === 'python' ? 'python' : 'r';
    const entryRef: { current?: Entry } = {};

    // Anything the caller left out falls back to what this machine has.
    const env = defaultEnvironment();
    const resolved = kernelType === 'python'
        ? {
            pythonHome: empty(options.pythonHome) ?? empty(env.pythonHome),
            pythonPath: empty(options.pythonPath),
            venvPath: empty(options.venvPath)
        }
        : {
            rHome: empty(options.rHome) ?? empty(env.rHome),
            rPath: empty(options.rPath) ?? (empty(options.rHome) ? undefined : empty(env.rPath)),
            rLibs: empty(options.rLibs) ?? empty(env.rLibs)
        };

    const session = await manager.createSession({
        kernelType,
        ...resolved,
        workingDirectory,
        enableLogging: true,
        logger: (level, message, data) => {
            if (entryRef.current) broadcast(entryRef.current, { event: 'log', level, message, data });
        }
    });

    const entry: Entry = {
        id,
        name: empty(options.name) ?? (kernelType === 'python' ? 'Python session' : 'R session'),
        session,
        kernelType,
        status: 'ready',
        // Kept so a refreshed page can rebuild the "Session Details" panel.
        config: resolved,
        workingDirectory,
        running: 0,
        clients: new Set()
    };
    entryRef.current = entry;
    sessions.set(id, entry);

    // Not also forwarding session.on('stdout'): StreamHandler emits it from
    // the same 'stream' iopub message this listener already receives, so
    // rendering both would duplicate every print()/cat() line.
    session.on('message', (message: WireMessage & { raw?: string }) => {
        broadcast(entry, { event: 'message', message: toWire(message) });
    });
    session.on('exit', (info: { reason?: string } | undefined) => {
        entry.status = 'crashed';
        broadcast(entry, { event: 'exit', reason: info?.reason });
    });
    session.on('stopped', () => {
        entry.status = 'stopped';
        broadcast(entry, { event: 'stopped' });
    });
    session.on('restarted', () => {
        entry.status = 'ready';
        broadcast(entry, { event: 'restarted' });
    });
    // EventEmitter's 'error' is special-cased by Node: with no listener it
    // throws and takes the whole server down. The lib re-emits every R
    // error's evalue (a plain string) under this name, which is already shown
    // -- with its traceback -- via the 'message' event, so only genuine Error
    // instances (a real connection/protocol problem) are forwarded.
    session.on('error', (error: unknown) => {
        if (error instanceof Error) {
            broadcast(entry, { event: 'connectionError', message: error.message });
        }
    });

    return entry;
}

export function summarize(entry: Entry): SessionSummary {
    return {
        id: entry.id,
        name: entry.name,
        status: entry.status,
        kernelType: entry.kernelType,
        config: entry.config,
        workingDirectory: entry.workingDirectory
    };
}
