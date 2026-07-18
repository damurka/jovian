import type { Middleware } from './middleware.js';

export class MiddlewareChain {
    private middlewares: Middleware[] = [];

    use(middleware: Middleware): void {
        this.middlewares.push(middleware);
    }

    async process(message: string): Promise<string> {
        let result = message;
        
        for (const middleware of this.middlewares) {
            result = await middleware.process(result);
        }
        
        return result;
    }
}
