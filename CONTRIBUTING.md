# Contributing to Jovian

Thanks for helping. Bug reports, fixes, docs and new ideas are all welcome. This page is the short version of how to do that without wasting your time or ours.

## Before you start

- **Found a bug or have a question?** Open an [issue](https://github.com/damurka/jovian/issues/new/choose). The forms ask for what is needed to reproduce it (versions, platform, how you installed it, the output with `JOVIAN_LOG_LEVEL=debug`). Check [Troubleshooting](docs/troubleshooting.md) first; it lists the real error messages and what causes them.
- **Want to change something non-trivial** (a new option, a new platform, a change to the protocol handling or the process model)? Open an issue and say what you have in mind before writing code, so we can agree on the shape of it. Small fixes and doc corrections do not need that; send the pull request.
- **Security problems** go to the repository's Security tab (private vulnerability reporting), not to a public issue.

## Set up

You need a C++23 toolchain, CMake, vcpkg, Node.js 24 and R (Python for the Python kernel). [Building from source](docs/building.md) has the requirements per platform; the short version:

```sh
npm install --legacy-peer-deps
npm run build            # native kernels into dist/native, then TypeScript into dist/lib
npm test                 # native ctest + TypeScript unit + integration tests
```

[Development](docs/development.md) explains the repository layout, the build system, the test layers and how to debug.

## The rules

1. **Every change that alters behavior comes with a test.** There are three layers: native tests (`ctest`), TypeScript unit tests (no processes) and integration tests (real supervisor, real R and Python kernels). Put the test at the lowest layer that can catch the bug. The TypeScript tests import the *compiled* library from `dist/`, so run `npm run build:lib` after changing `lib/`, or you are testing stale code.
2. **It has to work on every platform we ship.** CI builds and tests Windows x64, Linux x64 and arm64, and macOS x64 and arm64, and all five must be green. Keep platform-specific code behind the existing platform checks and say so in the pull request when you could only test one.
3. **A test that fails on one platform is a bug to understand, not a job to re-run.** The intermittent macOS failures were a real ordering race between the reply and the output, and re-running only hid it. If a failure only shows up on a slow runner, assume there is a race and find it.
4. **Keep the change focused.** One pull request, one purpose. No drive-by refactors, reformatting of code you did not change, or new abstractions for hypothetical needs. Match the surrounding code.
5. **Comments explain why, not what.** A constraint, a subtle invariant, a bug a line works around. Names carry the "what". Do not write comments that refer to the task or the issue you are fixing; that belongs in the commit message.
6. **No new runtime dependencies without discussing it first.** The npm package has none, on purpose. On the native side, dependencies come from `vcpkg.json` and each one costs a build on five platforms.
7. **Update the docs with the code.** If you change an option, an event, an error message or a command, update the page that describes it (`docs/`, and the README only if it is about installing or using the package). Keep the README short.
8. **Style.** TypeScript is strict ES modules with `.js` import extensions. C++ is C++23, formatted with `npm run format`. `npm run lint` runs ESLint and clang-tidy.
9. **Commits and pull requests.** Work on a branch and open a pull request against `main`. Small commits with a short imperative subject and a body that says *why*. Do not commit secrets, build output (`dist/`, `vcpkg_installed/`), tarballs, or files from your editor.
10. **You do not release.** Versions come from git tags, and a maintainer pushes `vX.Y.Z` to run the release workflow (see [Releasing](docs/releasing.md)). Do not bump versions in a pull request.

## Native code

Adrastea, Elara, Carpo and Themisto run several threads per process, so a few things are worth knowing before you touch them: ZeroMQ sockets are not thread-safe (a socket used from two threads needs a lock, as `DealerChannel` has), and R and Python are each embedded in one process on one thread (see [Architecture](docs/architecture/overview.md)). Interrupt and shutdown paths differ between Windows and POSIX; test both.

## Adding a platform

See [Releasing](docs/releasing.md#adding-a-platform). A platform needs a real R for it, a runner that can build and test it, and a passing dry run of the release workflow.

## Licensing

Jovian is [MIT licensed](LICENSE). By contributing you agree that your contribution is released under the same license. The R package in `packages/hera` keeps its own license (`packages/hera/LICENSE`).
