// The README example, type-checked under the strictest settings a consumer is
// likely to have (`tsc --init` enables exactOptionalPropertyTypes). Never run.
import { SessionManager } from '../../dist/lib/index.js';

export async function readmeExample(): Promise<void> {
    const manager = new SessionManager();

    // process.env.X is `string | undefined`: it must be accepted as-is.
    const r = await manager.createSession({ kernelType: 'r', rHome: process.env.R_HOME, workingDirectory: process.env.PROJECT_DIR });
    const result = await r.execute('x <- 1:10; mean(x)', { timeout: process.env.TIMEOUT ? Number(process.env.TIMEOUT) : undefined });
    console.log(result.success, result.output);

    const py = await manager.createSession({ kernelType: 'python', pythonHome: process.env.PYTHONHOME });
    console.log((await py.execute('sum(range(1, 11))')).success);
    await py.interrupt({ timeout: undefined });
    await r.restart({ rHome: process.env.R_HOME });

    await manager.stopAll();
}
