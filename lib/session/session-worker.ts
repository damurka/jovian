// Entry point for a forked child process hosting exactly one DatasuiteEngine
// (one embedded R interpreter, one ZMQ server) -- see session-manager.ts for
// why this needs to be a separate OS process rather than another engine
// instance in the same one: R only supports a single embedded interpreter
// per process, and isn't safe for concurrent use even within that limit.
import { DatasuiteEngine } from '../core/engine.js';
import type { EngineOptions, ExecutionOptions, ExecutionResult, ShinyAppOptions } from '../types/index.js';

const FORWARDED_EVENTS = ['ready', 'stopped', 'stdout', 'result', 'error', 'message'] as const;

type InitMessage = { type: 'init'; options: EngineOptions };
type ExecuteMessage = { id: string; type: 'execute'; code: string; options: ExecutionOptions };
type CreateShinyMessage = { id: string; type: 'createShiny'; options: ShinyAppOptions };
type StopMessage = { type: 'stop' };
type InboundMessage = InitMessage | ExecuteMessage | CreateShinyMessage | StopMessage;

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

// Error instances don't survive IPC's JSON serialization (their message and
// name aren't own-enumerable properties), so flatten before sending.
function flattenResult(result: ExecutionResult) {
    return {
        ...result,
        error: result.error ? { message: result.error.message, name: result.error.name } : undefined
    };
}

let engine: DatasuiteEngine | undefined;

async function handleExecute(message: ExecuteMessage): Promise<void> {
    if (!engine) return;
    try {
        const result = await engine.execute(message.code, message.options);
        process.send?.({ id: message.id, type: 'executeResult', result: flattenResult(result) });
    } catch (error) {
        process.send?.({ id: message.id, type: 'executeError', error: errorMessage(error) });
    }
}

async function handleCreateShiny(message: CreateShinyMessage): Promise<void> {
    if (!engine) return;
    try {
        const handle = await engine.createShiny(message.options);
        process.send?.({ id: message.id, type: 'shinyReady', host: handle.host, port: handle.port, url: handle.url });

        handle.done.then(
            (result) => process.send?.({ id: message.id, type: 'shinyDone', result: flattenResult(result) }),
            (error) => process.send?.({
                id: message.id,
                type: 'shinyDone',
                result: flattenResult({ success: false, output: [], error: error instanceof Error ? error : new Error(errorMessage(error)) })
            })
        );
    } catch (error) {
        process.send?.({ id: message.id, type: 'shinyStartError', error: errorMessage(error) });
    }
}

async function handleInit(message: InitMessage): Promise<void> {
    try {
        // logger is a callback and can't cross a process boundary -- use the
        // forwarded 'message'/'stdout' events on the Session object instead.
        const { logger: _logger, ...forwardable } = message.options;

        engine = new DatasuiteEngine(forwardable);

        for (const eventName of FORWARDED_EVENTS) {
            engine.on(eventName, (...args: unknown[]) => {
                process.send?.({ type: 'event', event: eventName, args });
            });
        }

        await engine.start();
        process.send?.({ type: 'workerReady' });
    } catch (error) {
        process.send?.({ type: 'startError', error: errorMessage(error) });
    }
}

async function handleStop(): Promise<void> {
    try {
        await engine?.stop();
    } finally {
        process.send?.({ type: 'stopped' });
        process.exit(0);
    }
}

process.on('message', (message: InboundMessage) => {
    switch (message.type) {
        case 'init': void handleInit(message); break;
        case 'execute': void handleExecute(message); break;
        case 'createShiny': void handleCreateShiny(message); break;
        case 'stop': void handleStop(); break;
    }
});
