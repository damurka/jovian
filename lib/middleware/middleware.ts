export interface Middleware {
    name: string;
    process(message: string): Promise<string> | string;
}
