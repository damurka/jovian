# Changelog

What each release of `@damurka/jovian` (and its platform packages, released together) changed for someone using it.
Versions come from tags (`docs/releasing.md`); the newest is first.

## Unreleased

- **`session.r` and `session.stata`** carry what only that kernel can do: `r.listPackages()`, `r.installPackages()`, `r.helpServer()`, `r.createShiny()` and the rest for an R session, `stata.dataset()` and `stata.data()` for a Stata session; `undefined` for any other kernel, so the type says what a session can do. The old names on `Session` (`session.listPackages()`, `session.stataDataset()`, `session.createShiny()`, ...) are gone: DataSuite, which pins its Jovian, moves to the new ones when it takes this version.
- **A kernel's registration is verified and matched to its launch.** The supervisor takes a registration only when its signature checks and it is the launch's own (`--registration-id`); any other is refused and that kernel ends. Nothing on the machine can name a session's ports but its kernel.
- **Large replies arrive 2 to 8 times sooner:** the supervisor no longer compresses WebSocket messages for a client on the same machine (a 4 MB page took 740 ms, now 190), and Stata's dataset answers are assembled at once (describing 2,000 variables: 185 ms, now 22).
- **A session is ready when its output is heard:** the supervisor waits for the kernel's `iopub_welcome` instead of 50 ms, so the first cell's output cannot be published before it listens.
- **Stopping and ending:** a kernel's last output is relayed, not dropped; a child process the kernel left behind (R's `system()`) no longer holds up its end; a kernel that answered `shutdown_request` and is ending on its own is given up to 12 s (2 s, as before, for one that is stuck).
- **The next cell waits for the interrupt** of a timed-out one, so the interrupt cannot land on it. `createShiny()` tries another port when its own choice was taken. `detach()` stops the manager's own packages and helper sessions. Two processes starting a persistent supervisor at once share one.
- The R debugger no longer writes to freed memory when a request timed out while R was stopped at a breakpoint.
- hera has tests of its own (`npm run test:hera`), run in CI on every platform and R version.
- The npm packages carry `THIRD_PARTY_NOTICES.md` (Adrastea began as a fork of QuantStack's xeus and xeus-zmq) and this changelog; the workspace declares Node >= 22.13; CI builds its native dependencies once.

## 0.2.10 (4 October 2026)

- **Sessions always start.** The kernels and the supervisor bind their ports and report them, instead of probing ports first and binding them later; about one session in 400 on a busy machine used to come up with a channel that never answered. A kernel started from a connection file (Jupyter, Kallichore) binds exactly the ports it was given, or exits with `cannot bind`.
- **Installs can't be cut off or doubled.** The packages session stays in use for as long as an install lasts (it was stopped five minutes after the install *began*); the library lock is refreshed by its holder and taken over only when the holder is gone; a session being started counts as using its library; helper R processes are stopped before packages are replaced.
- **A kernel that ended is known as ended:** `isStopped` is true, `execute()` rejects at once, and installs no longer wait for it.
- `loadedRPackages()` answers at once and never interrupts a running cell.
- The playground has a Packages tab; a guide for running the kernels from JupyterLab, `jupyter console` and `jupyter_client` (`docs/guides/jupyter.md`).
- CI is not repeated on the merge into `main`.

## 0.2.9 (4 October 2026)

- Hovering a keyword says what it is: R's reserved words show their help page; Python's show the language reference. Python's `inspect` reads the whole name under the cursor.

## 0.2.8 (4 October 2026)

- **Any installed Jupyter kernel runs under Themisto:** `kernelType: 'jupyter'` with `listJupyterKernels()`.
- **R packages install in a packages session** of the manager's (hera's `.jv.pkg.ensure()`), not in a script run by `Rscript`; updates wait only for the sessions that have what they replace loaded, or defer (`whenInUse`); Python installs coordinate with sessions the same way.
- `main` is protected: changes come by pull request once CI passed; releases stay tags.

## 0.2.7 (3 October 2026)

- `ensureRPackage()`, `ensurePythonPackages()` and `ensurePythonEnvironment()`: DataSuite's package installers, in Jovian.
- `findRuntime()` picks the R, Python or Stata to use; installations say how they were found and whether the kernel can run them.

## 0.2.6 (3 October 2026)

- hera, the R kernel's own R code, is built into Elara and loaded as `tools:jovian` with every name dot-named, as Ark lays out its R code; nothing is installed into R. The library and kernels must be from the same release from here on.
- `stop_on_error` aborts what is queued behind a failure before its reply goes out.
- A Stata error inside a loop is reported once, without the loop's echo.

## 0.2.5 (2 October 2026)

- Every R and Python installation on the machine is found, and the lists are exported (`listRInstallations()`, `listPythonInstallations()`, `listStataInstallations()`).

Earlier versions are described by their releases on GitHub.
