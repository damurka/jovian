<!-- README.md is generated from README.Rmd. Please edit that file -->

# hera

<!-- badges: start -->
<!-- badges: end -->

hera is the R side of the Elara Jupyter kernel (part of
[Jovian](https://github.com/damurka/jovian) -- originally adapted from
[xeus-r](https://github.com/jupyter-xeus/xeus-r)'s own R companion package):
running cells, rich output, plots, completion, inspection, comms, the
variables and data viewer, the debugger and the RStudio API.

## Not a package to install

hera is built into the `elara` executable (`cmake/EmbedHera.cmake` turns
these files into part of the kernel) and loaded when a session starts, the
way Ark carries its R code. Nothing is installed into R, and a `hera`
installed in your library by an older Jovian is not used.

As Ark's `tools:positron`, it is one locked environment on the search path,
`tools:jovian`, and every name in it is dot-named, so it masks no function:

- `.elara.*` -- what notebooks and packages call: `.elara.display()`,
  `.elara.display_data()`, `.elara.update_display_data()`,
  `.elara.clear_output()`, `.elara.cell_options()`, `.elara.host_ask()`,
  `.elara.host_notify()`, `.elara.CommManager`, `.elara.is_elara()`,
  `.elara.mime_bundle()`, `.elara.mime_types()`, `.elara.complete()`.
- `.jv.*` -- the kernel's own (`.jv.call()`, `.jv.rpc.call()`, ...), which
  the kernel and Jovian's library call as
  `as.environment("tools:jovian")$.jv.rpc.call(...)`.
- `View()` is the kernel's (it asks the application to show the data), put in
  `utils` itself rather than masking it.

Kernels before Jovian 0.2.6 loaded the same code as the namespace `hera`
(`hera::display()`); code that should work with both can look for
`tools:jovian` on `search()` first.

## Developing

Edit the files in `R/` and rebuild the kernel (`npm run build:native`): a
session always runs the hera built into its kernel. `NAMESPACE` lists only
the methods of base R's generics (`S3method(generic, class, function)`); the
kernel stops loading hera if any of its names lacks a dot.
