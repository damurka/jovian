// Shapes shared by the server route handlers and the browser UI.

export type KernelType = 'r' | 'python';
export type SessionStatus = 'starting' | 'ready' | 'stopped' | 'crashed';

/** A Jupyter message as the jovian lib hands it out (minus its raw JSON). */
export interface WireMessage {
    topic: string;
    msgType: string;
    channel: string;
    parentMsgId: string;
    content: any;
    timestamp?: number;
}

/** Server-sent events on GET /api/sessions/:id/stream. */
export type StreamEvent =
    | { event: 'connected'; status: SessionStatus }
    | { event: 'message'; message: WireMessage }
    | { event: 'log'; level: string; message: string; data?: unknown }
    | { event: 'exit'; reason?: string }
    | { event: 'stopped' }
    | { event: 'restarted' }
    | { event: 'connectionError'; message: string };

/** One past execution, as Session.getHistory() records it. */
export interface HistoryRecord {
    code: string;
    executionCount?: number;
    time: number;
    messages: WireMessage[];
}

export interface SessionConfig {
    rHome?: string;
    rPath?: string;
    rLibs?: string;
    pythonHome?: string;
    pythonPath?: string;
    venvPath?: string;
}

/** What GET /api/sessions lists (and what a refreshed page rebuilds from). */
export interface SessionSummary {
    id: string;
    name: string;
    status: SessionStatus;
    kernelType: KernelType;
    config: SessionConfig;
    workingDirectory?: string;
}

export interface Defaults {
    rHome: string;
    rPath: string;
    rLibs: string;
    pythonHome: string;
    pythonPath: string;
    venvPath: string;
    platform: string;
    homeDirectory: string;
}

export interface KernelInfo {
    language?: string;
    version?: string;
    implementation?: string;
    protocolVersion?: string;
    banner?: string;
}

export interface CompletionResult {
    ok: boolean;
    /** The kernel is running code (or did not answer in time) -- try again later. */
    busy?: boolean;
    error?: string;
    matches?: string[];
    cursorStart?: number;
    cursorEnd?: number;
}

export interface InspectResult {
    ok: boolean;
    busy?: boolean;
    error?: string;
    found?: boolean;
    text?: string;
}
