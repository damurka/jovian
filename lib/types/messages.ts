export type MessageTopic = 
    | 'stream'
    | 'execute_result'
    | 'display_data'
    | 'error'
    | 'execute_reply'
    | 'comm_open'
    | 'comm_msg'
    | 'comm_close'
    | 'status'
    | string;

export type MessageChannel = 'iopub' | 'shell' | 'stdin' | 'control';

export interface JupyterMessage<T = any> {
    topic: MessageTopic;
    msgType: string;
    channel: MessageChannel;
    parentMsgId: string;
    content: T;
    timestamp: number;
    raw: string;
}

export interface StreamContent {
    name: 'stdout' | 'stderr';
    text: string;
}

export interface ExecuteResultContent {
    execution_count: number;
    data: {
        'text/plain'?: string | string[];
        'text/html'?: string;
        'image/png'?: string;
        [key: string]: any;
    };
    metadata: Record<string, any>;
}

export interface ErrorContent {
    ename: string;
    evalue: string;
    traceback: string[];
}

export interface DisplayDataContent {
    data: Record<string, any>;
    metadata: Record<string, any>;
}

// Carried by the 'input_request' message a kernel sends on the stdin
// channel when running code calls input()/readline()/scan() during an
// execute() with allowStdin: true -- see Session's own 'input_request'
// event and sendInputReply() (lib/session/session-manager.ts) for how a
// caller answers it and unblocks the kernel's single execution thread.
export interface InputRequestContent {
    prompt: string;
    password: boolean;
}

// iopub 'status': the kernel's busy/idle bracket around every request it
// handles (execute, complete, inspect, ... -- not just execute). Session
// tracks the latest one as `session.executionState`.
export type ExecutionState = 'busy' | 'idle' | 'starting';

export interface StatusContent {
    execution_state: ExecutionState;
}

// iopub 'execute_input': the kernel announcing the code it is about to run
// (what Session.getHistory() buckets its per-execution entries by).
export interface ExecuteInputContent {
    code: string;
    execution_count: number;
}

// iopub 'clear_output': `wait: true` means "clear when the next output
// arrives", not right now (Jupyter's own semantics -- a client redraws
// without a flicker).
export interface ClearOutputContent {
    wait: boolean;
}

// iopub 'update_display_data': replaces the display previously published
// with the same `transient.display_id`.
export interface UpdateDisplayDataContent extends DisplayDataContent {
    transient?: { display_id?: string };
}

// Comms (iopub comm_open/comm_msg/comm_close, and the client -> kernel
// versions of the same three over shell): a named, bidirectional message
// stream between a frontend and a kernel-side target. Session.commOpen()/
// commMsg()/commClose() send them; the same messages from the kernel
// arrive as 'comm_open'/'comm_msg'/'comm_close' events.
export interface CommOpenContent {
    comm_id: string;
    target_name: string;
    data: Record<string, any>;
}

export interface CommMsgContent {
    comm_id: string;
    data: Record<string, any>;
}

export interface CommCloseContent {
    comm_id: string;
    data: Record<string, any>;
}
