# R, Python and Stata environments

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

### The `hera` package (built in)

Elara delegates code execution, rich output, completion, inspection and comms to `hera` (`packages/hera`), R code that is built into the kernel and loaded at start-up, as Ark carries its own R code. Nothing is installed into R and nothing has to be: `hera` imports no CRAN package, so a new session has only R's base packages loaded. As Ark's `tools:positron`, it is one locked environment on the search path, `tools:jovian`, and every name in it is dot-named: `.jv.*` are the kernel's own, `.elara.*` what notebooks and packages call (`.elara.display()`, `.elara.display_data()`, `.elara.clear_output()`, `.elara.cell_options()`, `.elara.host_ask()`, `.elara.host_notify()`, `.elara.CommManager`, ...). So it masks no function, a package's or base R's, and adds no namespace. `View()` is the kernel's (it asks the host to show the data), put in `utils` itself, as Ark does. Kernels before Jovian 0.2.6 loaded the same code as a namespace `hera` (`hera::display()`); a `hera` installed in your R library from older versions is not used.

`IRdisplay` (with `repr`), when installed, is loaded the first time something needs it: a data frame, an HTML widget, `.elara.display()`, `View()`. Without it these show as text.

For developing `hera`: edit `packages/hera` and rebuild the kernel (`npm run build:native`); a session always runs the copy built into its kernel. The kernel log (Themisto re-prints it with an `[elara]` prefix) says which it loaded: `loaded hera <version> (built in) as tools:jovian`.

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

## Stata

```typescript
await manager.createSession({
    kernelType: 'stata',
    stataHome: 'C:\\Program Files\\StataNow19', // optional: the directory holding Stata's executable
    stataEdition: 'se',                          // optional: when that directory has more than one edition
});
```

Stata sessions need **Stata 17 or newer**, installed and licensed. Callisto does not drive Stata's executable: it loads the shared library Stata ships next to it for its Python integration (pystata), and starts Stata inside the kernel process.

| Option | Effect |
|---|---|
| `stataHome` | Becomes `STATA_HOME` in the kernel (and `SYSDIR_STATA`, which Stata itself reads). When omitted it is `$STATA_HOME`, else the newest directory named `Stata<N>` / `StataNow<N>` under Program Files (Windows), `Stata*` under `/Applications` (macOS) or `stata*` under `/usr/local` (Linux) that has Stata's shared library in it; at the same version StataNow wins. |
| `stataEdition` | Which library to load: `mp-64.dll` / `se-64.dll` / `be-64.dll` (Windows), `libstata-mp.so` / `libstata-se.so` / `libstata.so` (Linux), `Stata<ED>.app/Contents/MacOS/libstata-<ed>.dylib` (macOS). Default: the first of MP, SE, BE present. |

**Ado-files and settings** are Stata's own: `sysdir`, `adopath`, `profile.do` and `net install`ed packages behave as in Stata's own console.

**Stata not usable?** `createSession()` rejects with `Kernel process exited before it could register …`, and the kernel's stderr (`[callisto] …`) says why: `No Stata shared library found in <dir> (Stata 17 or newer is needed)` for a Stata 16 or older (or a wrong directory), `Stata … could not start: Cannot find license file` when it is not licensed.

## Where kernels look for things — summary

| Setting | R | Python | Stata |
|---|---|---|---|
| Installation | `rHome` | `pythonHome` | `stataHome` (+ `stataEdition`) |
| Extra packages | `rLibs` | `venvPath` (+ `pythonPath`) | Stata's `adopath` |
| Shared-library path (POSIX) | `<rHome>/lib` added to `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` by the supervisor before the kernel starts | `<pythonHome>/lib`, likewise | loaded by full path from `stataHome` |
| Working directory | `workingDirectory` | `workingDirectory` | `workingDirectory` |

## Choosing at runtime

A session left without `rHome` / `pythonHome` / `stataHome` uses the first installation Jovian finds. An application that lets people choose — or needs a minimum version — asks Jovian what is there:

```ts
import { listRInstallations, listPythonInstallations, listStataInstallations, findRuntime, readRLibraries } from '@damurka/jovian';

// Every installation, best first: { home, version, label, source, usable, problem?, executable? (Python), editions? / licensed? (Stata) }
const pythons = await listPythonInstallations();

// The one to use: the first that is usable and new enough, else the first found (usable / meetsMinimum say why not)
const r = await findRuntime('r', { minVersion: '4.1.0' });
// A setting's choice is kept even when it is too old or not usable, so the user can be told; undefined if it is not R at all
const chosen = await findRuntime('python', { home: settings.pythonPath, minVersion: '3.10' });

// R's own user and site libraries, to list after a library of your own in rLibs
const libraries = await readRLibraries(r.home);
```

- **`source`** — how it was found: `setting` (findRuntime's `home`), `env` (`R_HOME`, `PYTHONHOME`, `STATA_HOME`), `path`, `registry` (Windows), `launcher` (Windows `py`), `folder` (a usual install location).
- **`usable`** — whether Jovian's kernel can run it. A Python without its shared library (`python3XY.dll`, `libpython3.X.so` / `.dylib`) can't be embedded by the Python kernel; a Stata without `stata.lic` won't start. `problem` says why, for people.
- **Order** — R: `$R_HOME`, `R RHOME`, the registry, the usual folders. Python: `$PYTHONHOME`, on Windows the `py` launcher's default, `python3` / `python` on `PATH`, pyenv / conda / macOS frameworks, then every Python the launcher knows (the Store's `python.exe` alias is skipped). Stata: licensed first, then newest, StataNow before Stata.
