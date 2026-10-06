# hera's own tests: Rscript packages/hera/tests/run.R (npm run test:hera). They need testthat; IRdisplay is optional.
# hera is not installed: tests/testthat/helper-hera.R loads R/*.R as Elara does.
args <- commandArgs(trailingOnly = FALSE)
this <- sub("^--file=", "", grep("^--file=", args, value = TRUE))
tests <- if (length(this)) dirname(normalizePath(this[[1L]])) else normalizePath("packages/hera/tests")
Sys.setenv(HERA_DIR = normalizePath(file.path(tests, "..")))
if (!requireNamespace("testthat", quietly = TRUE)) {
  stop("hera's tests need testthat: install.packages(\"testthat\")", call. = FALSE)
}
testthat::test_dir(file.path(tests, "testthat"), reporter = "summary", stop_on_failure = TRUE)
