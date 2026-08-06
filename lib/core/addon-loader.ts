import { createRequire } from 'module';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import type { Logger } from '../utils/logger.js';
import type { EngineOptions } from '../types/index.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

export class AddonLoader {
    static load(options: EngineOptions, logger: Logger): any {
        const requireNode = createRequire(import.meta.url);
        const addonPath = join(__dirname, '../../native/Release/datasuite_addon.node');
        
        logger.debug(`Loading native addon from: ${addonPath}`);
        try {
            const addon = requireNode(addonPath);
            logger.debug('Native addon loaded successfully');

            // Default to the 'hera' package bundled with this npm package
            // (dist/lib/core -> package root -> packages/hera) unless the
            // caller points at a different copy.
            const heraSrcPath = options.heraSrcPath ?? join(__dirname, '../../../packages/hera');
            const engineConfig = {
                rHome: options.rHome,
                rPath: options.rPath,
                rLibs: options.rLibs,
                pandocPath: options.pandocPath,
                heraSrcPath
            };

            // Logged even though it duplicates the config passed in -- this
            // is the exact set of values the native addon receives after
            // EnvironmentSetup/defaulting, which is what actually matters
            // when diagnosing "R wasn't found"-type failures.
            logger.info('Calling createEngine', engineConfig);
            addon.createEngine(engineConfig);
            logger.debug('createEngine completed');

            return addon;
        } catch (err: any) {
            logger.error(`Failed to load native addon: ${err.message}`, { code: err.code, stack: err.stack });
            throw err;
        }
    }
}
