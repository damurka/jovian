# R and Python environments

Jovian does not bundle R or Python. Each session points at an installation you choose, so different sessions (or a restarted one) can use different versions.

## R

```typescript
await manager.createSession({
    kernelType: 'r',
    rHome: 'C:/Program Files/R/R-4.6.0',          // R_HOME — required
    rPath: 'C:/Program Files/R/R-4.6.0/bin/x64',  // Windows: where R.dll is (default: <rHome>/bin/x64)
    rLibs: 'D:/r-libs',                            // extra library path, where hera goes
    pandocPath: 'C:/tools/pandoc',                 // optional
});
```

| Option | Effect |
|---|---|
| `rHome` | Becomes `R_HOME` for the kernel. Find yours with `R RHOME` (or `Sys.getenv("R_HOME")` inside R). On Linux/macOS Elara loads `<rHome>/lib/libR.so` / `libR.dylib`; on Windows `R.dll` is found through `PATH`. |
| `rPath` | Windows: directory containing `R.dll`/`Rblas.dll`, prepended to the kernel's `PATH` *before* it starts (an executable's DLL imports are resolved at process load). Defaults to `<rHome>/bin/x64`. |
| `rLibs` | Sets `R_LIBS` and `R_LIBS_USER` (and `R_LIBS_SITE` on Windows). Packages — including `hera` — are looked up and auto-installed here. |
| `pandocPath` | Sets `RSTUDIO_PANDOC` and prepends the directory to `PATH`, so R Markdown-style rendering works without a system pandoc. |

### The `hera` package (required)

Elara delegates code execution, completion, inspection and comms to the R package `hera` (`packages/hera`). **Without it R code cannot run**: the kernel still starts (it logs `WARNING: 'hera' package could not be loaded`), but every `execute` fails with `R evaluation of hera:::hera_call("execute", ...) failed (is the 'hera' package installed?)`.

Install it once into the library the session uses:

```r
install.packages(c("cli", "evaluate", "glue", "IRdisplay", "jsonlite", "R6", "repr", "rlang"))
```
```sh
npm run hera:install                   # = R CMD INSTALL packages/hera, from the repo root
```

or let Elara install it: set `heraSrcPath` to the `packages/hera` directory (needs the `remotes` package). At start-up Elara then installs `hera` if it is missing **or older than the source** (it compares source-file modification times with the installed `DESCRIPTION`, so editing `packages/hera/R/*.R` takes effect on the next session start). The kernel log (Themisto re-prints it with an `[elara]` prefix) says what happened: `Successfully loaded 'hera' package`, `auto-installed from ELARA_HERA_SRC`, `older than ELARA_HERA_SRC -- reinstalled`, or `WARNING: 'hera' package could not be loaded (status: …)` with one of `no_source_configured`, `source_not_found`, `remotes_unavailable`, `install_failed`.

`heraSrcPath` has **no default** — without it an R session uses whichever `hera` is already installed. Keep that copy current (`npm run hera:install`, or `heraSrcPath`): `hera` >= 0.6.0.9001 streams the output of a single long-running expression live (see [Kernels](../kernels.md#executing-code-hera)); an older one still works but shows such output only when the expression ends.

### Notes

- **Shared library.** On Linux, R must be built with `--enable-R-shlib` (packaged R is). Elara fails with an actionable message if `libR.so` is not under `<rHome>/lib`.
- **R ≥ 4.2 on Windows** for `readline()` over the stdin channel; older R still runs code.
- **Encoding.** On Windows Elara switches R's locale to UTF-8 (`Sys.setlocale('LC_ALL', '.UTF-8')`) so UTF-8 content does not raise native-encoding warnings.
- The working directory is the process's — set `workingDirectory` rather than calling `setwd()` in every session if all your relative paths hang off one project folder.

## Python

```typescript
await manager.createSession({
    kernelType: 'python',
    pythonHome: '/usr',                       // the prefix that contains lib/libpython3.x.so (or python3NN.dll on Windows)
    venvPath: '/projects/analysis/.venv',     // optional: make this venv's packages importable
    pythonPath: '/projects/shared',           // optional: extra PYTHONPATH
});
```

| Option | Effect |
|---|---|
| `pythonHome` | Becomes `PYTHONHOME`. Carpo scans it for the newest `python3NN.dll` (Windows, directly under the prefix), `lib/libpython3.*.so*` (Linux) or `lib/libpython3.*.dylib` (macOS) and loads it. Find it with `python -c "import sys; print(sys.base_prefix)"`. |
| `pythonPath` | Sets `PYTHONPATH`. |
| `venvPath` | Sets `CARPO_VENV_PATH`; on start-up the bootstrap prepends the venv's `site-packages` (`Lib/site-packages` on Windows, `lib/pythonX.Y/site-packages` on POSIX) to `sys.path`. |

**venvs:** an embedded interpreter needs the *base* installation's libpython and standard library, so `pythonHome` must point at the **base** install (`sys.base_prefix`), never at the venv (inside a venv `sys.prefix` has neither) — and `venvPath` is what makes the venv's packages importable.

**No Python installed?** Sessions of `kernelType: 'python'` fail to create: `createSession()` rejects with `Kernel process exited before it could register …` and the kernel's stderr (`[carpo] …`) says `No python3NN.dll was found directly under python_home …` / `No libpython3.*.so*/.dylib was found under …`. A Python without its shared library (some Linux/pyenv builds without `--enable-shared`) will not work either.

## Where kernels look for things — summary

| Setting | R | Python |
|---|---|---|
| Installation | `rHome` | `pythonHome` |
| Extra packages | `rLibs` | `venvPath` (+ `pythonPath`) |
| Shared-library path (POSIX) | `<rHome>/lib` added to `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` by the supervisor before the kernel starts | `<pythonHome>/lib`, likewise |
| Working directory | `workingDirectory` | `workingDirectory` |

## Choosing at runtime

Your application decides the paths; Jovian only consumes them. The playground has a discovery module (`tools/playground/lib/env.mjs`) that finds R and Python on Windows, macOS and Linux (env vars, `R RHOME`, `python3`, the Windows registry, common install locations) — a good reference if you need the same.
