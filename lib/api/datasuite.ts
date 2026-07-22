import { DatasuiteEngine } from '../core/engine.js';
import type { EngineOptions } from '../types/index.js';
import type { JupyterMessage } from '../types/messages.js';

/**
 * Create and start a Datasuite R kernel
 */
export async function createKernel(options: EngineOptions = {}): Promise<DatasuiteEngine> {
    const engine = new DatasuiteEngine(options);
    await engine.start();
    return engine;
}

/**
 * Execute R code with a simple API
 */
export async function executeR(code: string, options?: EngineOptions): Promise<string> {
    const engine = await createKernel(options);

    try {
        const result = await engine.execute(code);

        if (!result.success) {
            throw result.error ?? new Error('R execution failed');
        }

        return result.output
            .map(extractText)
            .filter((text): text is string => !!text)
            .join('');
    } finally {
        await engine.stop();
    }
}

function extractText(message: JupyterMessage): string | undefined {
    if (message.msgType === 'stream') {
        return message.content?.text;
    }

    const data = message.content?.data;
    if (!data) {
        return undefined;
    }

    const textPlain = data['text/plain'];
    return Array.isArray(textPlain) ? textPlain.join('\n') : textPlain;
}
