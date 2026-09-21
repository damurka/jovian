// Typed wrappers over the playground's route handlers (app/api/**).
import type {
    CompletionResult, Defaults, HistoryRecord, InspectResult, KernelInfo, KernelType, SessionSummary
} from '../types.ts';

async function request<T>(url: string, init?: RequestInit): Promise<T> {
    const res = await fetch(url, init);
    return (await res.json()) as T;
}

const post = (url: string, body?: unknown) =>
    request<any>(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: body === undefined ? undefined : JSON.stringify(body)
    });

export interface NewSessionRequest {
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

export const api = {
    defaults: () => request<Defaults>('/api/defaults'),
    sessions: () => request<{ sessions: SessionSummary[] }>('/api/sessions'),

    async createSession(body: NewSessionRequest): Promise<{ id: string }> {
        const res = await fetch('/api/sessions', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        const data = await res.json();
        if (!res.ok) throw new Error(data.error ?? res.statusText);
        return data;
    },

    removeSession: (id: string) => request<{ ok: boolean }>(`/api/sessions/${id}`, { method: 'DELETE' }),
    info: (id: string) => request<{
        pid?: number;
        memoryBytes?: number | null;
        heartbeat?: { hasPong: boolean; rttMs: number; sinceLastPongMs: number; misses: number } | null;
    }>(`/api/sessions/${id}/info`),
    kernelInfo: (id: string) => request<KernelInfo>(`/api/sessions/${id}/kernel-info`),
    history: (id: string) => request<{ history: HistoryRecord[] }>(`/api/sessions/${id}/history`),

    execute: (id: string, code: string, timeout?: number) =>
        post(`/api/sessions/${id}/execute`, { code, timeout }) as Promise<{
            ok: boolean; success: boolean; executionCount?: number; error?: string;
        }>,
    sendInput: (id: string, value: string) => post(`/api/sessions/${id}/input`, { value }),
    interrupt: (id: string) => post(`/api/sessions/${id}/interrupt`) as Promise<{ ok: boolean; acknowledged: boolean }>,
    restart: (id: string) => post(`/api/sessions/${id}/restart`) as Promise<{ ok: boolean; error?: string }>,
    stop: (id: string) => post(`/api/sessions/${id}/stop`),

    complete: (id: string, code: string, cursorPos: number, signal?: AbortSignal, noWait = false) =>
        request<CompletionResult>(`/api/sessions/${id}/complete`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ code, cursorPos, noWait }),
            signal
        }),
    inspect: (id: string, code: string, cursorPos: number, signal?: AbortSignal, noWait = false) =>
        request<InspectResult>(`/api/sessions/${id}/inspect`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ code, cursorPos, noWait }),
            signal
        })
};
