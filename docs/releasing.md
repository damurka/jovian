# Releasing

Jovian is published to npm as six packages under the `@damurka` scope, one library and five platform packages (Windows x64, Linux x64 and arm64, macOS x64 and arm64; see [Platforms that are not supported](#platforms-that-are-not-supported)):

| Package | Contents |
|---|---|
| `@damurka/jovian` | The compiled TypeScript library and docs. Lists the platform packages below as `optionalDependencies`. This is the one users install. (`hera`, the R kernel's R code, is inside `elara` in the platform packages.) |
| `@damurka/jovian-win32-x64` | `themisto.exe`, `elara.exe`, `carpo.exe`, `callisto.exe` and the DLLs they need. |
| `@damurka/jovian-linux-x64`, `-linux-arm64`, `-darwin-x64`, `-darwin-arm64` | `themisto`, `elara`, `carpo`, `callisto`. |

Each platform package declares `os` and `cpu`, so npm installs only the one that matches the machine. At run time the library finds the binaries in that package (see [`lib/session/native-paths.ts`](../lib/session/native-paths.ts): `JOVIAN_NATIVE_DIR`, then the platform package, then a source checkout's `dist/native/Release`). All four packages are published at the **same version**; the main package pins the platform packages to it.

The repository's own `package.json` is `"private": true` — it is the development workspace and is never published. Everything published is staged by `scripts/release.mjs` under `dist/release/`.

## One-time setup

1. **The scope.** `@damurka` must be a user or organization you can publish to on npmjs.com. If you use a different scope, change it in two places — `SCOPE` in `scripts/release.mjs` and `PACKAGE_SCOPE` in `lib/session/native-paths.ts` (a unit test fails if they differ) — plus the names in `README.md`, `docs/`, and the tarball globs in `.github/workflows/release.yml`.
2. **Trusted publishing (no token).** Each of the six packages is registered on npmjs.com (package → Settings → Trusted Publisher) with this GitHub repository (`damurka/jovian`) and the workflow file **`release.yml`** as its publisher. The publish job then needs no secret: npm (≥ 11.5.1) exchanges the job's OIDC identity (`id-token: write`) for a short-lived credential, and every publish carries provenance. The workflow file name is part of the registration, so renaming `release.yml` breaks publishing until it is registered again. There is no `NPM_TOKEN` secret to keep; delete it if it is still there. A package that does not exist yet cannot be registered: publish its first version by hand (below), then register it.
3. Scoped packages are private by default on npm; the packages carry `publishConfig.access: public`, and the workflow passes `--access public`.
4. Provenance (the badge that links a version to the workflow run that built it) needs the GitHub repository to be public.

## The first release is published by hand

A trusted publisher can only be registered for a package that **already exists** on the registry, so the workflow cannot publish a package's very first version. That one (`0.1.0`) is published from your machine, where npm can ask for your 2FA code:

```bash
npm login
# the tarballs CI built and smoke-tested (download them from the Release run's artifacts, or `gh run download <run-id>`):
npm publish ./tarballs-win32-x64/damurka-jovian-win32-x64-0.1.0.tgz --access public
npm publish ./tarballs-linux-x64/damurka-jovian-linux-x64-0.1.0.tgz --access public
npm publish ./tarballs-darwin-arm64/damurka-jovian-darwin-arm64-0.1.0.tgz --access public
npm publish ./tarballs-linux-x64/damurka-jovian-0.1.0.tgz --access public   # last: it depends on the three above
```

The main package goes last so nothing depends on a version that is not there yet. This first version carries no provenance badge. From the next version on, use the workflow.

## Cutting a release

1. Make sure `main` is green on CI.
2. Decide the version (semver). Nothing in the repository holds it: it comes from the tag.
3. Tag and push:

   ```bash
   git tag v0.1.0
   git push origin v0.1.0
   ```

4. `release.yml` runs. It calls `ci.yml` for the work, which builds once and tests in parallel, as r-universe does:
   - **Build:** the TypeScript library and its unit tests, once (`library`, which also packs the main package), and the kernels, once per platform, with their native tests (`native <platform>`, which also packs the platform package).
   - **Test, in parallel, against those builds:** the integration tests per platform on the oldest R and Python the kernels support (R oldrel-1, Python 3.10) and on the current ones (R release, the latest Python), plus R-devel on Linux, which warns without holding the release back (`integration ...`); and the **smoke test of the packed tarballs** per platform (`smoke <platform>`: `scripts/release-smoke.mjs` installs the platform tarball and the main tarball into an empty project, outside the repository, with an **empty R library** -- which must stay empty -- and starts a real R kernel and a real Python kernel from them).

   The same `ci.yml` runs on every push to `main` and pull request, without the packaging and the smoke test.
5. Only if every platform passed does the `publish` job run (in `release.yml` itself, which is why it must keep that name). It **publishes** the packages with `npm publish --provenance` (trusted publishing: no token, no approval), platform packages first, then the main package, which depends on them. A version already on npm is skipped, so a run that failed half way can be re-run.

   If npm refuses with a message about staged publishing or 2FA, the package's settings on npmjs.com require it: in each package's **Settings** > **Publishing access**, allow publishing from its trusted publisher without approval.

A version with a hyphen (`v0.2.0-rc.1`) is published under the `next` dist-tag, so it does not become what `npm install` picks by default.

### Dry run

Run the workflow by hand (Actions → Release → Run workflow). It does everything except publish (build, all tests, packing, the smoke test) and uploads the tarballs as artifacts, which you can download and `npm install` yourself.

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

## Platforms that are not supported

A platform is published only when there is a real R for it to pair with and a runner that can build and smoke-test it end to end.

- **Windows on ARM (`win32-arm64`).** R for Windows on ARM64 is experimental and not on CRAN (no binary packages either), so there is no R for a native ARM64 kernel to load. People on Windows on ARM run x64 R under emulation, which would need the x64 kernel, but npm installs the x64 package only on x64 CPUs. Not supported until R for Windows on ARM64 is released.
- **32-bit ARM Linux (`linux-armhf`).** GitHub's hosted runners cannot run 32-bit ARM code (the arm64 machines lack AArch32), so it can only be built by cross-compiling and tested under QEMU emulation, which is not set up.

Their users get `jovian: there are no prebuilt kernels for <os>-<cpu>` and can build from source and set `JOVIAN_NATIVE_DIR`.

## Adding a platform

1. Add the target to `TARGETS` in `scripts/release.mjs` and to `SUPPORTED_PLATFORMS` in `lib/session/native-paths.ts` (a unit test fails if they differ).
2. Add it to the build matrix in `.github/workflows/ci.yml` (release.yml reuses it).
3. Run the workflow by hand (a dry run) and fix what the smoke test finds before tagging a release.
4. A new platform is a **new package on npm**, whose first version the workflow cannot publish: publish it by hand once (see [The first release is published by hand](#the-first-release-is-published-by-hand)) and register `release.yml` as its trusted publisher. Until then the publish job stops with an error naming it, before the main package is published.

## Things to know

- **hera upgrades.** npm resets file modification times, so the kernel's usual "is the hera source newer than the installed one" check cannot fire for an npm install. The staged `hera` `DESCRIPTION` is stamped with `Config/jovian/release: <version>`, and the kernel reinstalls `hera` when that stamp differs from the installed copy's. Users therefore get the matching `hera` after upgrading the package, once.
- **Linux binaries and glibc.** They are built on `ubuntu-24.04` (glibc 2.39) and run on any distribution with at least that glibc. Building on an older image would widen compatibility; the `linux-arm64` build runs on `ubuntu-24.04-arm`.
- **macOS.** The build sets `MACOSX_DEPLOYMENT_TARGET=14.0`. The binaries are not code-signed or notarized; binaries installed through npm are not quarantined by Gatekeeper, so this works, but bundling them into a downloaded `.app` would require signing.
- **Windows.** The DLLs come from vcpkg's `x64-windows` triplet and are copied next to the executables; the binaries link the dynamic Visual C++ runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, checked with `dumpbin /dependents`), which the build copies next to them too (the `vc_runtime` target in `CMakeLists.txt`, from the Visual Studio that built them; Microsoft allows this "app-local" deployment). A clean Windows does not have the runtime -- Windows Sandbox, many new or locked-down PCs -- and without it the kernels exit with 0xC0000135 (DLL not found) before they start, so users need nothing else installed.
- **Executable bit.** npm does not reliably preserve it; the library `chmod`s the kernels before starting them.
- **Versions cannot be reused.** npm never lets a published version be republished. If a release fails half-way (say the platform packages published but the main package did not), bump the version and release again.
