// Shapes shared by the server route handlers and the browser UI.

export type KernelType = 'r' | 'python' | 'stata';

// The requests each session answers while a cell is running -- Python from
// another thread of its kernel (Interpreter::answersWhileBusy() in native/),
// R from a helper R process with the same packages attached (the library's
// r-helper.ts; something only the busy session has still waits); any other
// waits for the cell to finish.
const ANSWERED_WHILE_BUSY: Record<KernelType, readonly string[]> = {
    r: ['complete_request', 'inspect_request'],
    python: ['complete_request', 'inspect_request', 'is_complete_request'],
    stata: ['is_complete_request']
};

/** Whether `msgType` sent now would queue behind the cell this kernel is running. */
export function waitsForRunningCell(kernelType: KernelType, msgType: string): boolean {
    return !ANSWERED_WHILE_BUSY[kernelType]?.includes(msgType);
}
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
    stataHome?: string;
    stataEdition?: StataEdition;
}

export type StataEdition = 'mp' | 'se' | 'be';

/** What GET /api/sessions lists (and what a refreshed page rebuilds from). */
export interface SessionSummary {
    id: string;
    name: string;
    status: SessionStatus;
    kernelType: KernelType;
    config: SessionConfig;
    workingDirectory?: string;
}

/** One installation found on the server's machine (the library's RuntimeInstallation). */
export interface Installation {
    home: string;
    version?: string;
    /** "R 4.6.0", "Python 3.12.10", "StataNow 19". */
    label: string;
    /** Stata: the editions installed there, in the order they are tried. */
    editions?: StataEdition[];
    /** Stata: whether a stata.lic is there. */
    licensed?: boolean;
    /** R on Windows: the folder with R.dll. */
    rPath?: string;
}

export interface Defaults {
    rHome: string;
    rPath: string;
    rLibs: string;
    pythonHome: string;
    pythonPath: string;
    venvPath: string;
    stataHome: string;
    /** Everything found, per kernel; the homes above are the defaults among them. */
    installations: Record<KernelType, Installation[]>;
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
