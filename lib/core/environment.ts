import type { EngineOptions } from '../types/index.js';
import type { Logger } from '../utils/logger.js';
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

export class EnvironmentSetup {
    static configure(options: EngineOptions, logger: Logger): void {
        if (process.platform !== 'win32') return;

        if (options.rHome) {
            const rBinDir = `${options.rHome}/bin/x64`.replace(/\//g, '\\');
            
            if (!process.env.PATH?.includes(rBinDir)) {
                logger.info(`Adding ${rBinDir} to PATH...`);
                process.env.PATH = `${rBinDir};${process.env.PATH}`;
                logger.info('✓ R bin directory added to PATH');
            } else {
                logger.info(`R bin directory already in PATH: ${rBinDir}`);
            }
            
            const requireNode = createRequire(import.meta.url);
            const addonDir = join(__dirname, '../../native/Release').replace(/\//g, '\\');
            
            if (!process.env.PATH?.includes(addonDir)) {
                logger.info(`Adding addon directory to PATH: ${addonDir}`);
                process.env.PATH = `${addonDir};${process.env.PATH}`;
            }
        }
    }
}
