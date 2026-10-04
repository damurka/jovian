# Jovian and Jupyter

Jovian's kernels speak the Jupyter protocol, so the two connect in both directions:

- **Jovian's kernels in Jupyter**: JupyterLab, Jupyter Notebook, `jupyter console`, `jupyter execute`, nbconvert, VS Code's notebooks — anything that starts a kernel from a kernelspec — can run `elara` (R), `carpo` (Python) and `callisto` (Stata). No supervisor and no Node are involved.
- **Jupyter's kernels in Jovian**: a `SessionManager` can run any kernel installed for Jupyter (`kernelType: 'jupyter'`); see [Kernels](../kernels.md#any-installed-jupyter-kernel-kerneltype-jupyter).

This page is the first direction.

## 1. Write the kernelspecs

A kernelspec is a folder with a `kernel.json` that tells Jupyter how to start the kernel. From a source checkout, after `npm run build`:

```sh
npm run jupyter:kernelspec        # writes kernelspec/elara, kernelspec/carpo, kernelspec/callisto
```

It finds R, Python and Stata as `SessionManager` does and writes their locations into each file (`--only=r`, `--only=python` or `--only=stata` writes one; a runtime that isn't found is skipped with a warning). Run it again after moving an installation or the build.

From an **npm install** the kernels are in the platform package (`node_modules/@damurka/jovian-<os>-<cpu>/bin`); write the file yourself:

```json
{
  "argv": ["C:/app/node_modules/@damurka/jovian-win32-x64/bin/elara.exe",
           "-f", "{connection_file}",
           "--r-home", "C:/Program Files/R/R-4.6.0"],
  "display_name": "R (Elara)",
  "language": "R",
  "interrupt_mode": "message"
}
```

| Kernel | `argv` after the executable | `language` |
|---|---|---|
| `elara` | `-f {connection_file} --r-home <R home>` (optional: `--r-path`, `--r-libs`, `--pandoc-path`) | `R` |
| `carpo` | `-f {connection_file} --python-home <base Python prefix>` (optional: `--python-path`, `--venv-path`) | `python` |
| `callisto` | `-f {connection_file} --stata-home <Stata folder>` (optional: `--stata-edition mp\|se\|be`) | `stata` |

- **`interrupt_mode` must be `"message"`.** The kernels stop a cell on an `interrupt_request`; with Jupyter's default (`signal`) the Interrupt button would do nothing on Windows.
- **Linux and macOS:** the kernel must find the runtime's shared library. The generator adds `"env": { "LD_LIBRARY_PATH": "<home>/lib" }` (`DYLD_LIBRARY_PATH` on macOS); do the same in a hand-written file.

## 2. Install them

```sh
jupyter kernelspec install kernelspec/elara    --user --name elara
jupyter kernelspec install kernelspec/carpo    --user --name carpo
jupyter kernelspec install kernelspec/callisto --user --name callisto
jupyter kernelspec list
```

`--user` installs for you (`%APPDATA%\jupyter\kernels`, `~/Library/Jupyter/kernels`, `~/.local/share/jupyter/kernels`); without it, for the Python environment `jupyter` runs from. `jupyter kernelspec remove elara` removes one.

## 3. Use them

```sh
jupyter lab                                  # "R (Elara)", "Python (Carpo)", "Stata (Callisto)" in the launcher
jupyter console --kernel elara
jupyter execute --kernel_name=elara analysis.ipynb
```

From Python, with `jupyter_client`:

```python
from jupyter_client.manager import KernelManager

km = KernelManager(kernel_name="elara")
km.start_kernel()
kc = km.client()
kc.start_channels()
kc.wait_for_ready(timeout=40)
kc.execute_interactive("mean(c(1, 2, 3))")
km.interrupt_kernel()        # an interrupt_request, because of interrupt_mode "message"
kc.stop_channels()
km.shutdown_kernel()
```

## What you get, and what you don't

A Jupyter frontend gets what the kernels implement of the Jupyter protocol ([Protocol](../protocol.md) lists the messages): cells, results, printed output, plots, errors, completion, inspection (including help for keywords), input requests (`readline()`, `input()`), interrupt and shutdown.

What belongs to Jovian's library and supervisor is not there: session management and crash detection, restart with other options, the helper that answers completion while an R cell runs, package installs coordinated with running sessions, Shiny apps, and the host requests DataSuite adds (`View()`, the variables and data views).

## Ports

Jupyter chooses the five ports, writes them in the connection file and connects to them; the kernel binds exactly those. If one of them was taken between Jupyter choosing it and the kernel binding it, the kernel exits with

```
fatal: cannot bind tcp://127.0.0.1:<port>: Address in use (the port was chosen by whoever started this process, and something else has it)
```

and Jupyter reports that the kernel failed to start; starting it again picks new ports. (A kernel started by Jovian's supervisor is given no ports: it binds free ones itself and reports them, so this cannot happen there.)

## Checked

With `jupyter_client` 8.10 and `nbclient` on Windows, from kernelspecs installed as above: each kernel started 25 times in a row, ran a cell, answered the heartbeat and shut down; a notebook per kernel executed (a result, printed output, an R plot as an image, an error); a 30-second cell stopped at once on interrupt; completion answered for R and Python. JupyterLab's own interface was not driven as part of this check.
