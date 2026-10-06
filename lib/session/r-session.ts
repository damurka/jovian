// What only an R session (Elara) can do, as `session.r`: its packages, R's help server, a Shiny app in it. Answered
// by the kernel's own R code (hera's .jv.rpc.* in packages/hera/R, as Ark's .ps.rpc.*), through the session's RPC
// (a user expression of a silent execution: no output, no execution count); installs by the session manager's
// installer, in a packages session of its own.
import type {
    ExecutionResult, RPackageCheck, RPackageInfo, RPackageInstallResult, RPackageOptions, RPackageSearchResult,
    RPackageUpdate, ShinyAppHandle, ShinyAppOptions
} from '../types/index.js';
import { findFreePort, waitForPort } from '../utils/network.js';
import { delimiter } from 'path';
import type { Session } from './session-manager.js';

export class RSession {
    /** @internal */
    constructor(private readonly session: Session) { }

    // ---- packages ----

    /** The packages installed in the session's libraries. */
    async listPackages(options: Pick<RPackageOptions, 'timeout'> = {}): Promise<RPackageInfo[]> {
        return this.session.rpc<RPackageInfo[]>('pkg_list', {}, options.timeout ?? 60_000);
    }

    /** Whether the packages are installed, at least at the versions given (`{ dplyr: '1.1.4' }`). */
    async packagesInstalled(packages: string[], minVersions: Record<string, string> = {}): Promise<RPackageCheck[]> {
        const rows = await this.session.rpc<Array<Partial<RPackageCheck>>>('is_installed', { packages, min_versions: minVersions }, 60_000);
        return rows.map((row) => ({ name: row.name ?? '', version: row.version ?? null, installed: row.installed === true }));
    }

    /** Installed packages with a newer version in the repositories. */
    async outdatedPackages(options: RPackageOptions = {}): Promise<RPackageUpdate[]> {
        return this.session.rpc<RPackageUpdate[]>('pkg_outdated', { repos: options.repos ?? [] }, options.timeout ?? 120_000);
    }

    /** Packages in the repositories whose name matches `query` (an exact match first). */
    async searchPackages(query: string, options: RPackageOptions & { limit?: number } = {}): Promise<RPackageSearchResult[]> {
        return this.session.rpc<RPackageSearchResult[]>('pkg_search', { query, repos: options.repos ?? [], limit: options.limit ?? 100 }, options.timeout ?? 120_000);
    }

    /**
     * Installs packages, and what they need, from the repositories (`options.repos` before CRAN), or updates them to
     * the newest there, into `options.lib` (the session's first library by default). The session manager's installer
     * does it (SessionManager.ensureRPackage()), in a packages session of its own, not in this one: its progress
     * arrives as this session's 'stdout' events. A package this session has loaded can't be replaced on Windows.
     */
    async installPackages(packages: string[], options: RPackageOptions = {}): Promise<RPackageInstallResult> {
        const rHome = this.session.options.rHome;
        const installer = this.session.installer;
        if (!installer || !rHome) {
            throw new Error('installPackages() needs an R session created by a SessionManager');
        }
        const libraries = [options.lib, ...(this.session.options.rLibs ?? '').split(delimiter)]
            .filter((library): library is string => !!library);
        const result: RPackageInstallResult = { installed: [], failed: [], warnings: [] };
        for (const name of packages) {
            try {
                const done = await installer({ name, repos: options.repos ?? [], update: true, optional: false }, {
                    rHome,
                    libraries,
                    timeoutMs: options.timeout ?? 30 * 60_000,
                    // this session may have the packages loaded itself: waiting would be waiting for itself
                    whenInUse: 'proceed',
                    onOutput: (line) => this.session.emit('stdout', `${line}\n`)
                });
                result.installed.push({ name, version: done.version ?? null });
            } catch (error) {
                result.installed.push({ name, version: null });
                result.failed.push(name);
                result.warnings.push(error instanceof Error ? error.message : String(error));
            }
        }
        return result;
    }

    /** Removes packages from the library they are installed in. */
    async removePackages(packages: string[], options: Pick<RPackageOptions, 'lib' | 'timeout'> = {}): Promise<string[]> {
        const args: Record<string, unknown> = { packages };
        if (options.lib) args.lib = options.lib;
        const result = await this.session.rpc<{ removed: string[] }>('remove_packages', args, options.timeout ?? 120_000);
        return result.removed ?? [];
    }

    /**
     * The packages the session has loaded (loadedNamespaces(): their DLLs are in use), asked of it now; undefined
     * when that can't be known -- code is running (it may load anything). Asking never interrupts a running cell.
     */
    loadedPackages(): Promise<string[] | undefined> {
        return this.session.loadedRPackages();
    }

    // ---- help ----

    /**
     * R's own help server in the session (tools::startDynamicHelp()), started if need be: its port and base
     * address. It answers while the session is idle.
     */
    async helpServer(): Promise<{ port: number; url: string }> {
        return this.session.rpc<{ port: number; url: string }>('help_server', {}, 30_000);
    }

    /** The help server's address for a help topic (in `pkg`, else wherever it is found), or null. */
    async helpUrl(topic: string, pkg?: string): Promise<string | null> {
        const args: Record<string, unknown> = { topic };
        if (pkg) args.package = pkg;
        return this.session.rpc<string | null>('help_url', args, 30_000);
    }

    // ---- Shiny ----

    /**
     * Launches a Shiny app in this session's R process and resolves once it's actually accepting connections.
     * shiny::runApp() blocks the R session for as long as the app runs, so -- unlike execute() -- resolving here does
     * not mean the app is done; that's what the returned `done` promise is for.
     */
    async createShiny(options: ShinyAppOptions): Promise<ShinyAppHandle> {
        await this.session.ready();
        const log = this.session.log;

        const host = options.host ?? '127.0.0.1';
        const launchBrowser = options.launchBrowser ?? false;
        const readyTimeout = options.readyTimeout ?? 10000;
        const appDir = rStringLiteral(options.appDir.replace(/\\/g, '/'));
        const setEnvPrefix = buildSetEnvCode(options.env);

        // Shiny has to be told its port, and binds it only once R gets to runApp(): another process can take a
        // port found free here in between. When the app says so (httpuv's "Failed to create server"), and the
        // port was this method's choice, another is tried.
        for (let attempt = 1; ; attempt++) {
            const port = options.port ?? await findFreePort(host);
            const code = `${setEnvPrefix}shiny::runApp(${appDir}, port = ${port}, host = '${host}', launch.browser = ${launchBrowser ? 'TRUE' : 'FALSE'})`;
            log.info('Starting Shiny app', { appDir: options.appDir, host, port, readyTimeout });

            // timeout: 0 -- this call is expected to block indefinitely.
            const done = this.session.execute(code, { timeout: 0 });
            done.then(
                (result) => log.info(`Shiny app at ${host}:${port} exited`, { success: result.success }),
                (error) => log.error(`Shiny app at ${host}:${port} execution failed`, error)
            );

            const earlyExit = done.then((result) => {
                const error = new Error(`Shiny app exited before it started listening (status: ${result.success ? 'ok' : 'error'})`);
                (error as Error & { portTaken?: boolean }).portTaken = portWasTaken(result);
                throw error;
            });
            earlyExit.catch(() => { });

            try {
                await Promise.race([waitForPort(host, port, readyTimeout), earlyExit]);
            } catch (error) {
                if ((error as { portTaken?: boolean }).portTaken && options.port === undefined && attempt < 3) {
                    log.warn(`Shiny app could not bind ${host}:${port} (taken since it was found free): trying another port`);
                    continue;
                }
                log.error(`Shiny app at ${host}:${port} failed to start`, error);
                throw error;
            }

            log.info(`Shiny app listening at http://${host}:${port}`);
            return { host, port, url: `http://${host}:${port}`, done };
        }
    }
}

// Whether a Shiny app's execution ended because its port was taken: httpuv's error, or R's for a socket.
function portWasTaken(result: ExecutionResult): boolean {
    const texts = [result.error?.message ?? ''];
    for (const message of result.output) {
        const content = message.content as { text?: string; evalue?: string } | undefined;
        if (content?.text) texts.push(content.text);
        if (content?.evalue) texts.push(content.evalue);
    }
    return /Failed to create server|address already in use|EADDRINUSE/i.test(texts.join('\n'));
}

function rStringLiteral(value: string): string {
    return `'${value.replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;
}

function buildSetEnvCode(env: Record<string, string> | undefined): string {
    if (!env || Object.keys(env).length === 0) {
        return '';
    }

    const args = Object.entries(env)
        .map(([key, value]) => `${rStringLiteral(key)} = ${rStringLiteral(value)}`)
        .join(', ');

    return `Sys.setenv(${args}); `;
}
