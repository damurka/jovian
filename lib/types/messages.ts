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

export type MessageChannel = 'iopub' | 'shell' | 'stdin';

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
