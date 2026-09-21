# Releasing

Jovian is published to npm as four packages under the `@damurka` scope (Windows x64, Linux x64 and macOS arm64; see [Adding a platform](#adding-a-platform) for the others):

| Package | Contents |
|---|---|
| `@damurka/jovian` | The compiled TypeScript library, the `hera` R package, docs. Lists the platform packages below as `optionalDependencies`. This is the one users install. |
| `@damurka/jovian-win32-x64` | `themisto.exe`, `elara.exe`, `carpo.exe` and the DLLs they need. |
| `@damurka/jovian-linux-x64`, `-darwin-arm64` | `themisto`, `elara`, `carpo`. |

Each platform package declares `os` and `cpu`, so npm installs only the one that matches the machine. At run time the library finds the binaries in that package (see [`lib/session/native-paths.ts`](../lib/session/native-paths.ts): `JOVIAN_NATIVE_DIR`, then the platform package, then a source checkout's `dist/native/Release`). All four packages are published at the **same version**; the main package pins the platform packages to it.

The repository's own `package.json` is `"private": true` — it is the development workspace and is never published. Everything published is staged by `scripts/release.mjs` under `dist/release/`.

## One-time setup

1. **The scope.** `@damurka` must be a user or organization you can publish to on npmjs.com. If you use a different scope, change it in two places — `SCOPE` in `scripts/release.mjs` and `PACKAGE_SCOPE` in `lib/session/native-paths.ts` (a unit test fails if they differ) — plus the names in `README.md`, `docs/`, and the tarball globs in `.github/workflows/release.yml`.
2. **A token.** On npmjs.com create an *automation* access token (or a granular token with read/write on the `@damurka` packages) and add it to the GitHub repository as the secret **`NPM_TOKEN`** (Settings → Secrets and variables → Actions). If your account enforces 2FA for publishing, the token must be of the automation kind, which bypasses the prompt.
3. Scoped packages are private by default on npm; the packages carry `publishConfig.access: public`, and the workflow passes `--access public`.
4. The workflow publishes with **provenance** (`--provenance`, needs the `id-token: write` permission it declares), which requires the GitHub repository to be public.

## Cutting a release

1. Make sure `main` is green on CI.
2. Decide the version (semver). Nothing in the repository holds it: it comes from the tag.
3. Tag and push:

   ```bash
   git tag v0.1.0
   git push origin v0.1.0
   ```

4. `release.yml` runs. For each platform it builds the native binaries in Release, compiles the library, stages both packages, packs them, and **smoke-tests the packed tarballs**: `scripts/release-smoke.mjs` installs the platform tarball and the main tarball into an empty project (outside the repository, with an empty R library so the bundled `hera` has to install from the package) and starts a real R kernel and a real Python kernel from them.
5. Only if every platform passed does the `publish` job run: the platform packages first, then the main package (so nothing ever depends on a version that is not there yet).

A version with a hyphen (`v0.2.0-rc.1`) is published under the `next` dist-tag, so it does not become what `npm install` picks by default.

### Dry run

Run the workflow by hand (Actions → Release → Run workflow). It does everything except publish and uploads the tarballs as artifacts, which you can download and `npm install` yourself.

### Locally

```bash
npm run build                                                    # native + TypeScript
node scripts/release.mjs platform --version 0.1.0                # this machine's platform package
node scripts/release.mjs main --version 0.1.0
mkdir -p dist/release/tarballs
(cd dist/release/jovian-win32-x64 && npm pack --pack-destination ../tarballs)   # your platform's name
(cd dist/release/jovian && npm pack --pack-destination ../tarballs)
node scripts/release-smoke.mjs --dir dist/release/tarballs --python
```

Nothing there publishes. Publishing by hand is `npm publish <tarball> --access public`, platform packages first.

## Adding a platform

Only the platforms that have been built and smoke-tested end to end are published (Windows x64, Ubuntu x64 and macOS arm64, the three the CI matrix covers). To add `linux-arm64` or `darwin-x64`:

1. Add the target to `TARGETS` in `scripts/release.mjs` and to `SUPPORTED_PLATFORMS` in `lib/session/native-paths.ts` (a unit test fails if they differ).
2. Add it to the build matrix in `.github/workflows/release.yml` (`ubuntu-24.04-arm`, `macos-15-intel`); the job `env` already sets `MACOSX_DEPLOYMENT_TARGET: '14.0'` for macOS.
3. Run the workflow by hand (a dry run) and fix what the smoke test finds before tagging a release.

Until a platform is added, its users get `jovian: there are no prebuilt kernels for <os>-<cpu>` and can build from source and set `JOVIAN_NATIVE_DIR`.

## Things to know

- **hera upgrades.** npm resets file modification times, so the kernel's usual "is the hera source newer than the installed one" check cannot fire for an npm install. The staged `hera` `DESCRIPTION` is stamped with `Config/jovian/release: <version>`, and the kernel reinstalls `hera` when that stamp differs from the installed copy's. Users therefore get the matching `hera` after upgrading the package, once.
- **Linux binaries and glibc.** They are built on `ubuntu-24.04` (glibc 2.39) and run on any distribution with at least that glibc. Building on an older image would widen compatibility; a `linux-arm64` build would use `ubuntu-24.04-arm`.
- **macOS.** The build sets `MACOSX_DEPLOYMENT_TARGET=14.0`. The binaries are not code-signed or notarized; binaries installed through npm are not quarantined by Gatekeeper, so this works, but bundling them into a downloaded `.app` would require signing.
- **Windows.** The DLLs come from vcpkg's `x64-windows` triplet and are copied next to the executables; the binaries link the dynamic Visual C++ runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`, checked with `dumpbin /dependents`), which is **not** bundled: users need the "Microsoft Visual C++ Redistributable" (x64, 2015–2022), and the README's Install section says so. Bundling the runtime DLLs into the package, or linking the runtime statically, would remove that requirement.
- **Executable bit.** npm does not reliably preserve it; the library `chmod`s the kernels before starting them.
- **Versions cannot be reused.** npm never lets a published version be republished. If a release fails half-way (say the platform packages published but the main package did not), bump the version and release again.
