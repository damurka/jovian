// Answers completion and inspection for an R session while it is busy.
//
// R's interpreter runs one thing at a time and cannot be called from another
// thread, so an R kernel answers complete_request / inspect_request only after
// the cell it is running finishes (Python's kernel answers them mid-run; see
// Interpreter::answersWhileBusy() in native/). For the question that matters
// most while code runs -- "what is this function?" -- the answer does not
// depend on the busy session's state, only on which packages it has attached:
// help for purrr::walk is the same everywhere. So a second, idle R process
// (one per R installation, started on first need and shared by the sessions
// that use it) attaches the same packages and answers instead. It cannot see
// objects the busy session created; completion adds their names from the
// last time the session finished a cell, and inspecting one of them waits for
// the cell, as before.
import type { UserExpressionResult } from '../types/index.js';

/** What an R session had attached and defined when it last finished a cell. */
export interface RSessionState {
    /** Attached packages, from search(): "purrr", "stats", ... */
    packages: string[];
    /** Names in the global environment (at most 5000). */
    globals: string[];
}

/** The user_expressions key an R execution carries to report its state (removed from the result). */
export const R_STATE_KEY = '.jovian_state';
export const R_STATE_EXPRESSION = 'jsonlite::toJSON(list(search = search(), globals = utils::head(ls(globalenv()), 5000L)))';

/** The state from an R_STATE_EXPRESSION result, or undefined when it failed. */
export function parseRState(result: UserExpressionResult | undefined): RSessionState | undefined {
    if (!result || result.status !== 'ok') return undefined;
    const text = (result as { data?: Record<string, unknown> }).data?.['text/plain'];
    if (typeof text !== 'string') return undefined;
    try {
        const parsed = JSON.parse(text.trim()) as { search?: unknown; globals?: unknown };
        const strings = (value: unknown) => (Array.isArray(value) ? value.filter((v): v is string => typeof v === 'string') : []);
        return {
            packages: strings(parsed.search).filter((entry) => entry.startsWith('package:')).map((entry) => entry.slice('package:'.length)),
            globals: strings(parsed.globals)
        };
    } catch {
        return undefined;
    }
}

/** An R string literal. */
function rString(value: string): string {
    return `"${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

/** The R code that attaches `packages` (ignoring any that cannot be), quietly. */
export function attachCode(packages: string[]): string {
    return `for (.jovian_p in c(${packages.map(rString).join(', ')})) ` +
        'try(suppressPackageStartupMessages(library(.jovian_p, character.only = TRUE)), silent = TRUE); rm(.jovian_p)';
}

/**
 * Completion matches from the helper plus the busy session's own names that
 * match what is typed (the helper cannot see them), without duplicates.
 */
export function mergeCompletions(
    reply: { matches?: string[]; cursor_start?: number; cursor_end?: number },
    code: string,
    cursorPos: number,
    globals: string[]
): { matches: string[]; cursor_start: number; cursor_end: number } {
    const start = reply.cursor_start ?? cursorPos;
    const typed = [...code].slice(start, cursorPos).join('');
    const matches = [...(reply.matches ?? [])];
    if (typed) {
        for (const name of globals) {
            if (name.startsWith(typed) && !matches.includes(name)) matches.push(name);
        }
    }
    return { ...reply, matches, cursor_start: start, cursor_end: reply.cursor_end ?? cursorPos };
}

/** The part of a Session a helper needs. */
export interface HelperSession {
    execute(code: string, options?: { silent?: boolean; timeout?: number }): Promise<{ success: boolean }>;
    request<T>(msgType: string, content?: Record<string, unknown>, options?: { timeout?: number }): Promise<T>;
    stop(): Promise<void>;
    kill(): void;
    on(event: 'exit', listener: () => void): unknown;
}

const HELPER_REQUEST_TIMEOUT_MS = 30_000;

/** One helper R process, answering for the busy sessions of one R installation. */
export class RHelper {
    private session: Promise<HelperSession> | undefined;
    private readonly attached = new Set<string>();
    // One answer at a time: attaching packages and asking must not interleave.
    private chain: Promise<unknown> = Promise.resolve();

    constructor(private readonly create: () => Promise<HelperSession>) {}

    /** Starts the helper and attaches `state`'s packages ahead of the first question. */
    warm(state: RSessionState): void {
        void this.enqueue(async () => {
            await this.prepare(state);
        }).catch(() => {});
    }

    /** Asks the helper `msgType` with the busy session's packages attached. */
    ask<T>(msgType: string, content: Record<string, unknown>, state: RSessionState): Promise<T> {
        return this.enqueue(async () => {
            const helper = await this.prepare(state);
            return helper.request<T>(msgType, content, { timeout: HELPER_REQUEST_TIMEOUT_MS });
        });
    }

    async stop(): Promise<void> {
        const session = this.session;
        this.session = undefined;
        if (session) await (await session.catch(() => undefined))?.stop().catch(() => {});
    }

    kill(): void {
        const session = this.session;
        this.session = undefined;
        void session?.then((s) => s.kill(), () => {});
    }

    private enqueue<T>(work: () => Promise<T>): Promise<T> {
        const next = this.chain.then(work, work);
        this.chain = next.catch(() => {});
        return next;
    }

    private async prepare(state: RSessionState): Promise<HelperSession> {
        if (!this.session) {
            this.attached.clear();
            const starting = this.create();
            this.session = starting;
            starting.then((session) => {
                session.on('exit', () => {
                    if (this.session === starting) this.session = undefined;
                });
            }, () => {
                if (this.session === starting) this.session = undefined;
            });
        }
        const helper = await this.session;
        const missing = state.packages.filter((p) => !this.attached.has(p));
        if (missing.length > 0) {
            await helper.execute(attachCode(missing), { silent: true, timeout: HELPER_REQUEST_TIMEOUT_MS });
            for (const p of missing) this.attached.add(p);
        }
        return helper;
    }
}
