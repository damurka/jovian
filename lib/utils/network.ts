import * as net from 'net';

/**
 * Asks the OS for an available TCP port by binding to port 0 and reading
 * back what got assigned.
 */
export function findFreePort(host: string = '127.0.0.1'): Promise<number> {
    return new Promise((resolve, reject) => {
        const server = net.createServer();
        server.unref();
        server.once('error', reject);
        server.listen(0, host, () => {
            const address = server.address();
            const port = typeof address === 'object' && address ? address.port : undefined;
            server.close(() => {
                if (port) {
                    resolve(port);
                } else {
                    reject(new Error('Failed to allocate a free port'));
                }
            });
        });
    });
}

/**
 * Polls `host:port` until something accepts a TCP connection, or rejects
 * once `timeoutMs` elapses without one.
 */
export function waitForPort(host: string, port: number, timeoutMs: number): Promise<void> {
    const deadline = Date.now() + timeoutMs;

    return new Promise((resolve, reject) => {
        const attempt = () => {
            const socket = net.connect({ host, port }, () => {
                socket.destroy();
                resolve();
            });

            socket.once('error', () => {
                socket.destroy();
                if (Date.now() >= deadline) {
                    reject(new Error(`Timed out waiting for ${host}:${port} to accept connections`));
                } else {
                    setTimeout(attempt, 150);
                }
            });
        };

        attempt();
    });
}
