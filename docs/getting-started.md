# Getting started

This walks from a clean machine to a running R session and a running Python session. The [README](../README.md#requirements) has the precise requirements per platform; this page is the sequence.

## 1. Install the prerequisites

| Platform | Install |
|---|---|
| Windows | **Visual Studio** with the *Desktop development with C++* workload (the repo is built with Visual Studio 2026 / MSVC v145), **Git**, **Node.js** (recent LTS), **R** (4.2 or newer), optionally **Python 3**. |
| Linux | A C++23 compiler, `cmake`, Git, Node.js, R built with a shared library (`--enable-R-shlib`; distribution packages are), `uuid-dev`, optionally Python 3. |
| macOS | Xcode Command Line Tools, CMake, Git, Node.js, R, optionally Python 3. |

Then install **vcpkg** and point `VCPKG_ROOT` at it:

```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh          # Windows: .\bootstrap-vcpkg.bat
export VCPKG_ROOT=$PWD        # PowerShell: $env:VCPKG_ROOT = (Get-Location).Path
```

## 2. Install the R side

Elara needs the R package `hera` (in `packages/hera`) and its dependencies in the R library the session will use:

```r
install.packages(c("cli", "evaluate", "glue", "IRdisplay", "jsonlite", "R6", "repr", "rlang"))
```
```sh
npm run hera:install               # = R CMD INSTALL packages/hera, from the repo root; re-run it after pulling to pick up hera changes
```

(Alternatively pass `heraSrcPath` when creating a session and let Elara install it — see [Environments](guides/environments.md#the-hera-package-required).)

## 3. Build

```sh
npm install --legacy-peer-deps
npm run build          # native (elara, carpo, themisto) -> dist/native/Release, then TypeScript -> dist/lib
```

`npm run build` uses `VCPKG_ROOT`'s toolchain file; the first build compiles ZeroMQ, OpenSSL and the other vcpkg dependencies, which takes a while (vcpkg caches them afterwards). If it succeeds you have `dist/native/Release/{themisto,elara,carpo}[.exe]` and `dist/lib/`.

## 4. Your first R session

Save as `first-r.mjs` in the repo root and run `node first-r.mjs`:

```javascript
import { SessionManager } from './dist/lib/index.js';

const manager = new SessionManager();
const session = await manager.createSession({
    kernelType: 'r',
    rHome: process.env.R_HOME,                // e.g. "C:/Program Files/R/R-4.6.0" or the output of `R RHOME`
    rPath: process.env.R_PATH,                // Windows only: e.g. "C:/Program Files/R/R-4.6.0/bin/x64"
    workingDirectory: process.cwd(),          // where getwd() will point
});

session.on('error', () => {});                            // required: see docs/api/README.md#events
session.on('stdout', (text) => process.stdout.write(text));

const result = await session.execute('print("hello from R"); x <- 1:10; mean(x)');
console.log('success:', result.success);
console.log(result.output.map((m) => `${m.msgType}: ${JSON.stringify(m.content).slice(0, 80)}`));

console.log(await session.kernelInfo());                  // language_info.name === 'R'
console.log((await session.complete('pri')).matches);     // [ 'print', … ]

await manager.stopAll();                                  // always: it also ends the supervisor
```

## 5. Your first Python session

```javascript
const py = await manager.createSession({
    kernelType: 'python',
    pythonHome: process.env.PYTHONHOME,       // the prefix that contains libpython — python -c "import sys; print(sys.base_prefix)"
    workingDirectory: process.cwd(),
});
py.on('error', () => {});
py.on('stdout', (t) => process.stdout.write(t));
await py.execute('import os\nprint(os.getcwd())\nsum(range(1, 11))');
```

Create both in one script, side by side — they are separate processes.

## 6. Things worth trying next

```javascript
// Interactive input
session.on('input_request', ({ prompt }) => session.sendInputReply('World'));
await session.execute('name <- readline("name? "); cat("hello", name, "\\n")', { allowStdin: true });

// Interrupt a long call
const running = session.execute('Sys.sleep(60)', { timeout: 0 });
setTimeout(() => session.interrupt(), 500);
console.log((await running).success);                    // false — interrupted

// Evaluate an expression after the code
const r = await session.execute('x <- 21', { userExpressions: { double: 'x * 2' } });
console.log(r.userExpressions.double);                    // { status: 'ok', data: { 'text/plain': '[1] 42' }, … }
```

More: [interactive input](guides/interactive-input.md), [interrupting](guides/interrupting.md), [comms](guides/comms.md), [session lifecycle](guides/sessions-lifecycle.md), and the runnable [`examples/`](../examples).

## 7. The playground

A browser UI on top of the same API, handy for trying an installation:

```sh
npm run playground:install    # once
npm run playground            # http://127.0.0.1:4173
```

It pre-fills R and Python from auto-discovery, shows PID / memory / working directory per session, and supports `Tab` completion and `Shift+Tab` inspect. See [the playground guide](guides/playground.md).

## 8. Run the tests

```sh
npm test                 # native ctest + TypeScript unit + integration
```

If something fails to start, see [Troubleshooting](troubleshooting.md).
