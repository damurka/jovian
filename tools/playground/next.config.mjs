import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));

/** @type {import('next').NextConfig} */
const nextConfig = {
    // This app lives inside the jovian repo, which has its own lockfile;
    // pin the root so Next does not guess (and warn about) the wrong one.
    turbopack: { root: here },
    outputFileTracingRoot: here,
    // The dev server is bound to 127.0.0.1 (see scripts/next.mjs); allow
    // both spellings of loopback for HMR.
    allowedDevOrigins: ['127.0.0.1', 'localhost'],
    reactStrictMode: true,
    // Do not generate AGENTS.md/CLAUDE.md into the repo.
    agentRules: false
};

export default nextConfig;
