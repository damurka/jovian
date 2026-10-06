# Changelog

What each release of `@damurka/jovian` (and its platform packages, released together) changed for someone using it.
Versions come from tags (`docs/releasing.md`); the newest is first.

## Unreleased

Nothing yet.

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
