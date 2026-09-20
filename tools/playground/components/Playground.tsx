'use client';

import { useCallback, useEffect, useReducer, useRef, useState } from 'react';
import { api, type NewSessionRequest } from '@/lib/client/api';
import { canRun, initialState, newSessionView, reducer } from '@/lib/client/store';
import { useInspect } from '@/lib/client/use-inspect';
import type { Defaults, StreamEvent } from '@/lib/types';
import { ConsoleView, kernelLabel } from './ConsoleView';
import { InputDock } from './InputDock';
import { InspectorPanel } from './InspectorPanel';
import { InspectPopover } from './InspectPopover';
import { NewKernelModal } from './NewKernelModal';
import { Sidebar, kernelBadgeClass, statusDotClass } from './Sidebar';

const INFO_POLL_MS = 1000;

export function Playground() {
    const [state, dispatch] = useReducer(reducer, initialState);
    const [defaults, setDefaults] = useState<Defaults | null>(null);
    const [modalOpen, setModalOpen] = useState(false);
    const [draft, setDraft] = useState('');

    const sources = useRef(new Map<string, EventSource>());
    const loaded = useRef(false);
    // Async callbacks need the latest state, not the render they closed over.
    const stateRef = useRef(state);
    stateRef.current = state;

    const active = state.activeId ? state.sessions[state.activeId] ?? null : null;
    const inspector = useInspect(active?.id ?? null);

    // -- kernel version --------------------------------------------------

    // The kernel's own version, from a real kernel_info_request.
    const loadKernelVersion = useCallback(async (id: string) => {
        try {
            const info = await api.kernelInfo(id);
            if (info.version) dispatch({ type: 'patch', id, patch: { version: info.version } });
        } catch {
            // Best-effort: a failed lookup just leaves the version blank.
        }
    }, []);

    // -- live event stream ------------------------------------------------

    const handleStreamEvent = useCallback((id: string, payload: StreamEvent) => {
        switch (payload.event) {
            case 'message':
                dispatch({ type: 'message', id, message: payload.message });
                break;
            case 'exit':
                dispatch({ type: 'patch', id, patch: { status: 'crashed', running: false } });
                break;
            case 'stopped':
                dispatch({ type: 'patch', id, patch: { status: 'stopped', running: false } });
                break;
            case 'restarted':
                // A restarted kernel is a fresh interpreter: its counter starts over.
                dispatch({ type: 'patch', id, patch: { status: 'ready', execCount: 0, running: false } });
                // A restart can switch the interpreter (and so its version).
                void loadKernelVersion(id);
                break;
            case 'connectionError':
                dispatch({ type: 'notice', id, cls: 'error', text: `Connection error: ${payload.message}` });
                break;
            default:
                break;
        }
    }, [loadKernelVersion]);

    // One EventSource per known session, opened when it appears and closed
    // when it goes away.
    useEffect(() => {
        for (const id of state.order) {
            if (sources.current.has(id)) continue;
            const es = new EventSource(`/api/sessions/${id}/stream`);
            es.onmessage = (event) => {
                try {
                    handleStreamEvent(id, JSON.parse(event.data));
                } catch {
                    // ignore a malformed frame
                }
            };
            // EventSource retries on its own.
            es.onerror = () => {};
            sources.current.set(id, es);
        }
        for (const [id, es] of sources.current) {
            if (!state.order.includes(id)) {
                es.close();
                sources.current.delete(id);
            }
        }
    }, [state.order, handleStreamEvent]);

    useEffect(() => {
        const current = sources.current;
        return () => {
            for (const es of current.values()) es.close();
            current.clear();
        };
    }, []);

    // -- initial load -------------------------------------------------------

    // Repopulates the sidebar from the sessions the server still holds
    // (a page refresh wipes this page's state, not the kernels'), then
    // replays each one's transcript from Session.getHistory().
    useEffect(() => {
        if (loaded.current) return;
        loaded.current = true;

        api.defaults().then(setDefaults).catch((e) => console.error('Failed to load defaults', e));

        api.sessions().then(({ sessions }) => {
            for (const s of sessions) {
                dispatch({ type: 'add', session: newSessionView(s) });
                api.history(s.id)
                    .then(({ history }) => dispatch({ type: 'hydrate', id: s.id, history }))
                    .catch((e) => console.error('Failed to hydrate session history', e));
                if (s.status === 'ready') void loadKernelVersion(s.id);
            }
        }).catch((e) => console.error('Failed to reload existing sessions', e));
    }, [loadKernelVersion]);

    // -- pid / memory ---------------------------------------------------------

    // A snapshot of the selected session, polled: memory changes constantly
    // and is not something the kernel publishes as a protocol message.
    useEffect(() => {
        const id = state.activeId;
        if (!id) return;
        const poll = async () => {
            try {
                const info = await api.info(id);
                const hb = info.heartbeat;
                dispatch({
                    type: 'patch', id,
                    patch: {
                        pid: info.pid || null,
                        memoryBytes: typeof info.memoryBytes === 'number' ? info.memoryBytes : null,
                        heartbeat: hb
                            ? { rttMs: hb.hasPong ? hb.rttMs : null, sinceLastPongMs: hb.hasPong ? hb.sinceLastPongMs : null, misses: hb.misses }
                            : null
                    }
                });
            } catch {
                // keep the last known values until the next poll succeeds
            }
        };
        void poll();
        const timer = setInterval(poll, INFO_POLL_MS);
        return () => clearInterval(timer);
    }, [state.activeId]);

    // -- actions ---------------------------------------------------------------

    const runCode = useCallback(async (id: string, code: string, timeout?: number) => {
        if (!code.trim() || !stateRef.current.sessions[id]) return;
        const key = crypto.randomUUID();
        dispatch({ type: 'runStarted', id, key, code, time: Date.now() });
        try {
            const result = await api.execute(id, code, timeout);
            dispatch({
                type: 'runFinished', id, key,
                executionCount: result.executionCount,
                failure: result.success ? undefined : (result.error ?? '')
            });
        } catch (error) {
            dispatch({ type: 'runFinished', id, key });
            dispatch({ type: 'notice', id, cls: 'error', text: `Request failed: ${(error as Error).message}` });
        }
    }, []);

    const runDraft = () => {
        if (!active || !draft.trim()) return;
        const code = draft;
        setDraft('');
        void runCode(active.id, code);
    };

    const createSession = async (request: NewSessionRequest) => {
        const { id } = await api.createSession(request);
        const summary = (await api.sessions()).sessions.find((s) => s.id === id);
        if (!summary) throw new Error('The new session did not show up on the server.');
        dispatch({ type: 'add', session: newSessionView(summary) });
        dispatch({ type: 'select', id });
        setModalOpen(false);
        void loadKernelVersion(id);
    };

    const restart = async (id: string) => {
        const s = stateRef.current.sessions[id];
        if (!s) return;
        if (s.status === 'stopped') {
            dispatch({ type: 'notice', id, cls: 'system', text: 'This session was stopped -- create a new session instead of restarting it.' });
            return;
        }
        dispatch({ type: 'patch', id, patch: { status: 'starting' } });
        try {
            const body = await api.restart(id);
            if (!body.ok) throw new Error(body.error ?? 'restart failed');
            // On success the 'restarted' stream event flips status back to 'ready'.
        } catch (error) {
            dispatch({ type: 'notice', id, cls: 'error', text: `Restart failed: ${(error as Error).message}` });
            dispatch({ type: 'patch', id, patch: { status: 'crashed' } });
        }
    };

    const interrupt = async (id: string) => {
        try {
            const { acknowledged } = await api.interrupt(id);
            if (!acknowledged) {
                dispatch({ type: 'notice', id, cls: 'system', text: 'Interrupt sent; the kernel has not acknowledged it (busy running code, or unreachable).' });
            }
        } catch (error) {
            dispatch({ type: 'notice', id, cls: 'error', text: `Interrupt request failed: ${(error as Error).message}` });
        }
    };

    // Forgets the session here and on the server. Always kills it first, so
    // removing one never leaves an orphaned live kernel behind.
    const remove = async (id: string) => {
        await api.removeSession(id).catch(() => {});
        dispatch({ type: 'remove', id });
    };

    const exportLog = () => {
        if (!active) return;
        const blob = new Blob([JSON.stringify({
            id: active.id, name: active.name, kernelType: active.kernelType, status: active.status,
            workingDirectory: active.workingDirectory, cells: active.transcript.cells
        }, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `${active.id}-export.json`;
        document.body.appendChild(a);
        a.click();
        a.remove();
        URL.revokeObjectURL(url);
    };

    const sessions = state.order.map((id) => state.sessions[id]);
    const runnable = active ? canRun(active.status) : false;

    return (
        <div className="app">
            <header>
                <div className="brand-area">
                    <div className="brand-badge">J</div>
                    <div className="brand-title">
                        jovian playground
                        <span className="brand-tag">real sessions</span>
                    </div>
                </div>
                <div className="global-actions">
                    <button className="btn-secondary" id="btnNewSession" onClick={() => setModalOpen(true)}>
                        <span>+</span> New Kernel Session
                    </button>
                    <button className="btn-secondary" id="btnExportLog" disabled={!active} onClick={exportLog}>
                        <span>&#128190;</span> Export Session Log
                    </button>
                </div>
            </header>

            <div className="workspace">
                <Sidebar
                    sessions={sessions}
                    activeId={state.activeId}
                    onSelect={(id) => dispatch({ type: 'select', id })}
                    onRestart={() => active && void restart(active.id)}
                    onStop={() => active && void api.stop(active.id)}
                    onInterrupt={() => active && void interrupt(active.id)}
                    onRemove={() => active && void remove(active.id)}
                />

                <main className="center-stage">
                    <div className="toolbar-strip">
                        <div className="active-session-summary">
                            <span id="activeKernelDot" className={'status-dot' + (active ? ` ${statusDotClass(active)}` : '')} />
                            <strong id="activeSessionName">{active ? active.name : 'No session selected'}</strong>
                            {active && (
                                <span id="activeKernelBadge" className={`kernel-badge ${kernelBadgeClass(active.kernelType)}`}>
                                    {active.version ? `${kernelLabel(active.kernelType)} ${active.version}` : kernelLabel(active.kernelType)}
                                </span>
                            )}
                        </div>
                        <div className="kernel-controls">
                            <button className="btn-secondary" id="btnClearConsole" disabled={!active}
                                onClick={() => active && dispatch({ type: 'clearOutput', id: active.id })}>
                                <span>&#9003;</span> Clear Output
                            </button>
                        </div>
                    </div>

                    <ConsoleView
                        session={active}
                        hasSessions={sessions.length > 0}
                        onAnswerInput={(value) => {
                            if (!active) return;
                            dispatch({ type: 'answerInput', id: active.id, value });
                            void api.sendInput(active.id, value).catch((e) =>
                                dispatch({ type: 'notice', id: active.id, cls: 'error', text: `Failed to send input reply: ${e.message}` }));
                        }}
                        onInspect={(token, x, y) => void inspector.inspect(token, token, token.length, { kind: 'point', x, y }, active?.running)}
                    />

                    {active && (
                        <InputDock
                            session={active}
                            canRun={runnable}
                            value={draft}
                            onChange={setDraft}
                            onRun={runDraft}
                            onInterrupt={() => void interrupt(active.id)}
                            onClear={() => dispatch({ type: 'clearOutput', id: active.id })}
                            onInspect={(title, code, cursor, anchor, busy) => void inspector.inspect(title, code, cursor, anchor, busy)}
                        />
                    )}
                </main>

                <InspectorPanel
                    session={active}
                    onRunPreset={(code, timeout) => {
                        if (!active) return;
                        setDraft('');
                        void runCode(active.id, code, timeout);
                    }}
                />
            </div>

            <NewKernelModal open={modalOpen} defaults={defaults} onClose={() => setModalOpen(false)} onCreate={createSession} />
            <InspectPopover state={inspector.state} onClose={inspector.close} />
        </div>
    );
}
