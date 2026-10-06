// What only a Stata session (Callisto) can do, as `session.stata`: the dataset in its memory, read by the kernel
// itself (its Mata library and its plugin, native/src/callisto) and answered as a user expression of a silent
// execution: no output, no execution count.
import type { StataDataOptions, StataDataPage, StataDataset } from '../types/index.js';
import type { Session } from './session-manager.js';

export class StataSession {
    /** @internal */
    constructor(private readonly session: Session) { }

    /** The dataset in the session's memory: its frame, size, file, variables and value labels. */
    async dataset(options: { timeout?: number | undefined } = {}): Promise<StataDataset> {
        return this.session.stataCall<StataDataset>('.callisto_dataset', '', options.timeout ?? 60_000);
    }

    /**
     * Observations of the dataset in the session's memory: `count` (default 100, at most 100 000) from `start` (1, the
     * first), of `variables` (default all). Raw values -- numbers, `null` for the missing value `.`, `".a"` to `".z"`
     * for the extended ones, strings -- or, `formatted`, strings as Stata's Data Editor shows them (value labels,
     * display formats such as `%td` dates).
     */
    async data(options: StataDataOptions = {}): Promise<StataDataPage> {
        const request: Record<string, unknown> = {};
        if (options.start !== undefined) request.start = options.start;
        if (options.count !== undefined) request.count = options.count;
        if (options.variables !== undefined) request.variables = options.variables;
        if (options.formatted !== undefined) request.formatted = options.formatted;
        return this.session.stataCall<StataDataPage>('.callisto_data', JSON.stringify(request), options.timeout ?? 120_000);
    }
}
