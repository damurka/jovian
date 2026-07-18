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

export interface JupyterMessage<T = any> {
    topic: MessageTopic;
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
