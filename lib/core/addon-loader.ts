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
        
        logger.info(`Loading native addon from: ${addonPath}`);
        try {
            const addon = requireNode(addonPath);
            logger.info('✓ Addon loaded successfully');
            
            logger.info('Calling createEngine...');
            addon.createEngine({
                rHome: options.rHome,
                rPath: options.rPath,
                rLibs: options.rLibs
            });
            logger.info('✓ createEngine completed');
            
            return addon;
        } catch (err: any) {
            logger.error(`Failed to load native addon: ${err.message}`);
            logger.error(`Code: ${err.code}`);
            logger.error(`Stack: ${err.stack}`);
            throw err;
        }
    }
}
