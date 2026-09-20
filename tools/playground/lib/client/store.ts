// Client-side state as one pure reducer (no React imports), so the whole
// session lifecycle -- live messages, hydration after a refresh, run/finish
// bookkeeping -- can be unit-tested without a browser.
import {
    answerInput, applyMessage, clearCells, emptyTranscript, endCell, pushOutput, setCellNumber, startCell,
    type OutputClass, type Transcript
} from '../transcript.ts';
import type { HistoryRecord, KernelType, SessionConfig, SessionStatus, WireMessage } from '../types.ts';
import type { HeartbeatView } from './heartbeat.ts';

export interface SessionView {
    id: string;
    name: string;
    kernelType: KernelType;
    status: SessionStatus;
    config: SessionConfig;
    workingDirectory?: string;
    version: string | null;
    pid: number | null;
    memoryBytes: number | null;
    /** Latest kernel heartbeat, polled with pid/memory. */
    heartbeat: HeartbeatView | null;
    /** The kernel's own execution counter, as far as this page knows. */
    execCount: number;
    /** An execute() request is in flight. */
    running: boolean;
    transcript: Transcript;
}

export interface State {
    sessions: Record<string, SessionView>;
    order: string[];
    activeId: string | null;
}

export const initialState: State = { sessions: {}, order: [], activeId: null };

export type Action =
    | { type: 'add'; session: SessionView }
    | { type: 'remove'; id: string }
    | { type: 'select'; id: string | null }
    | { type: 'patch'; id: string; patch: Partial<Omit<SessionView, 'transcript'>> }
    | { type: 'message'; id: string; message: WireMessage }
    | { type: 'runStarted'; id: string; key: string; code: string; time: number }
    | { type: 'runFinished'; id: string; key: string; executionCount?: number; failure?: string }
    | { type: 'notice'; id: string; cls: OutputClass; text: string }
    | { type: 'answerInput'; id: string; value: string }
    | { type: 'clearOutput'; id: string }
    | { type: 'hydrate'; id: string; history: HistoryRecord[] };

export function newSessionView(fields: Pick<SessionView, 'id' | 'name' | 'kernelType' | 'status' | 'config' | 'workingDirectory'>): SessionView {
    return { ...fields, version: null, pid: null, memoryBytes: null, heartbeat: null, execCount: 0, running: false, transcript: emptyTranscript() };
}

function update(state: State, id: string, fn: (s: SessionView) => SessionView): State {
    const current = state.sessions[id];
    if (!current) return state;
    const next = fn(current);
    return next === current ? state : { ...state, sessions: { ...state.sessions, [id]: next } };
}

export function reducer(state: State, action: Action): State {
    switch (action.type) {
        case 'add':
            if (state.sessions[action.session.id]) return state;
            return {
                ...state,
                sessions: { ...state.sessions, [action.session.id]: action.session },
                order: [...state.order, action.session.id]
            };

        case 'remove': {
            const { [action.id]: _removed, ...rest } = state.sessions;
            return {
                sessions: rest,
                order: state.order.filter((id) => id !== action.id),
                activeId: state.activeId === action.id ? null : state.activeId
            };
        }

        case 'select':
            return { ...state, activeId: action.id };

        case 'patch':
            return update(state, action.id, (s) => ({ ...s, ...action.patch }));

        case 'message':
            return update(state, action.id, (s) => {
                const transcript = applyMessage(s.transcript, action.message);
                return transcript === s.transcript ? s : { ...s, transcript };
            });

        case 'runStarted':
            return update(state, action.id, (s) => {
                const inNum = s.execCount + 1;
                return {
                    ...s,
                    execCount: inNum,
                    running: true,
                    transcript: startCell(s.transcript, { key: action.key, inNum, code: action.code, time: action.time })
                };
            });

        case 'runFinished':
            return update(state, action.id, (s) => {
                let transcript = s.transcript;
                let execCount = s.execCount;
                // Reconcile with the kernel's authoritative count. Matters
                // most right after a reconnect, where this page's counter
                // restarts at 0 while the real kernel kept counting.
                if (typeof action.executionCount === 'number') {
                    transcript = setCellNumber(transcript, action.key, action.executionCount);
                    execCount = action.executionCount;
                }
                if (action.failure !== undefined) {
                    transcript = pushOutput(transcript, { kind: 'text', cls: 'system', text: action.failure ? `Execution failed: ${action.failure}` : 'Execution interrupted' });
                }
                return { ...s, execCount, running: false, transcript: endCell(transcript) };
            });

        case 'notice':
            return update(state, action.id, (s) => {
                // Output attaches to the running cell; with none running, to
                // the most recent one, so a notice is never silently lost.
                const lastKey = s.transcript.cells.at(-1)?.key ?? null;
                const t = s.transcript.currentKey ? s.transcript : { ...s.transcript, currentKey: lastKey };
                const withNotice = pushOutput(t, { kind: 'text', cls: action.cls, text: action.text });
                return { ...s, transcript: { ...withNotice, currentKey: s.transcript.currentKey } };
            });

        case 'answerInput':
            return update(state, action.id, (s) => ({ ...s, transcript: answerInput(s.transcript, action.value) }));

        case 'clearOutput':
            return update(state, action.id, (s) => ({ ...s, transcript: clearCells(s.transcript) }));

        case 'hydrate':
            // Replays each recorded execution through the exact reducer live
            // messages use, so a refreshed page renders identically.
            return update(state, action.id, (s) => {
                if (s.transcript.cells.length > 0 || action.history.length === 0) return s;
                let t = s.transcript;
                let execCount = s.execCount;
                action.history.forEach((record, i) => {
                    const inNum = typeof record.executionCount === 'number' ? record.executionCount : ++execCount;
                    execCount = Math.max(execCount, inNum);
                    t = startCell(t, { key: `history-${i}`, inNum, code: record.code, time: record.time });
                    for (const message of record.messages) t = applyMessage(t, message);
                });
                return { ...s, execCount, transcript: endCell(t) };
            });
    }
}

export function canRun(status: SessionStatus): boolean {
    return status !== 'stopped' && status !== 'crashed';
}
