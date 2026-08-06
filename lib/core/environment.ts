import type { EngineOptions } from '../types/index.js';
import type { Logger } from '../utils/logger.js';
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

export class EnvironmentSetup {
    static configure(options: EngineOptions, logger: Logger): void {
        if (process.platform !== 'win32') {
            logger.debug(`Skipping PATH setup on non-Windows platform (${process.platform})`);
            return;
        }

        if (!options.rHome) {
            // Not necessarily an error -- some callers rely on R already
            // being discoverable on PATH -- but it's exactly the kind of
            // thing worth a line when diagnosing why R wasn't found, since
            // no PATH setup happens at all in that case.
            logger.debug('No rHome configured; skipping R PATH setup (relying on R already being on PATH)');
            return;
        }

        // Prefer an explicit rPath over the derived default -- otherwise
        // this can add a *different* R bin directory to PATH than the
        // one native/src/bridge/datasuite_engine.cpp's setupEnvironment()
        // later uses for R_HOME/R_LIBS, since that C++ side always uses
        // rPath verbatim when it's given.
        const rBinDir = (options.rPath ?? `${options.rHome}/bin/x64`).replace(/\//g, '\\');

        if (!process.env.PATH?.includes(rBinDir)) {
            logger.debug(`Adding R bin directory to PATH: ${rBinDir}`);
            process.env.PATH = `${rBinDir};${process.env.PATH}`;
        } else {
            logger.debug(`R bin directory already in PATH: ${rBinDir}`);
        }

        const requireNode = createRequire(import.meta.url);
        const addonDir = join(__dirname, '../../native/Release').replace(/\//g, '\\');

        if (!process.env.PATH?.includes(addonDir)) {
            logger.debug(`Adding addon directory to PATH: ${addonDir}`);
            process.env.PATH = `${addonDir};${process.env.PATH}`;
        } else {
            logger.debug(`Addon directory already in PATH: ${addonDir}`);
        }
    }
}
