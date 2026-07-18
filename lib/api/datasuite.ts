import { DatasuiteEngine } from '../core/engine.js';
import type { EngineOptions } from '../types/index.js';

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
    
    return new Promise((resolve, reject) => {
        let result = '';
        
        engine.on('result', (text: string) => {
            result = text;
        });
        
        engine.on('error', (error: Error) => {
            reject(error);
        });
        
        engine.execute(code).then(() => {
            engine.stop().then(() => resolve(result));
        });
    });
}
