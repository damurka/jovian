# The installer's pure parts (R/packages-ensure.R): what it reads from a DESCRIPTION, how it compares versions, and
# what counts as a package's strong dependencies. The install itself runs in a packages session (integration tests).

test_that("requirements are read from a Depends or Imports field", {
  requirements <- jv(".jv.pkg.requirements")
  expect_identical(requirements("a (>= 1.0), b"), list(c("a", "1.0"), c("b", NA)))
  expect_identical(requirements("R (>= 4.1.0), methods"), list(c("R", "4.1.0"), c("methods", NA)))
  expect_identical(requirements("x(>=2)"), list(c("x", "2")))
  expect_identical(requirements("  dplyr  (>=  1.1.4 )  ,\n  rlang"), list(c("dplyr", "1.1.4"), c("rlang", NA)))
  expect_identical(requirements(NA), list())
})

test_that("a version meets a requirement, or not", {
  meets <- jv(".jv.pkg.meets")
  expect_true(meets("1.2.3", "1.2"))
  expect_true(meets("1.2", "1.2"))
  expect_false(meets("1.2", "1.2.1"))
  expect_false(meets("0.9.9", "1.0"))
  expect_true(meets("1.0", NA))
  expect_true(meets("1.0", ""))
  expect_false(meets(NA_character_, "1.0"))
  expect_false(meets(NA_character_, NA))
})

test_that("an installed package's version is read, and a missing one is NA", {
  version_of <- jv(".jv.pkg.version_of")
  expect_identical(version_of("stats"), as.character(utils::packageVersion("stats")))
  expect_identical(version_of("no.such.package.zz"), NA_character_)
})

test_that("R's own packages are known, and have no conflicts to find", {
  base <- jv(".jv.pkg.base")()
  expect_true(all(c("base", "stats", "utils", "methods") %in% base))
  conflicts <- jv(".jv.pkg.conflicts")("stats")
  expect_s3_class(conflicts, "data.frame")
  expect_identical(nrow(conflicts), 0L)
  expect_identical(names(conflicts), c("by", "by_version", "by_lib", "dep", "needs", "has", "has_lib"))
})

test_that("LinkingTo counts as strong only for a package compiled here", {
  strong_for <- jv(".jv.pkg.strong_for")
  db <- function(repo, compiled) {
    matrix(c(repo, compiled), nrow = 1L, dimnames = list("pkg", c("Repository", "NeedsCompilation")))
  }
  source_here <- identical(.Platform$pkgType, "source")
  compiled_from_cran <- strong_for("pkg", db("https://cran.r-project.org/src/contrib", "yes"))
  expect_identical("LinkingTo" %in% compiled_from_cran, source_here)
  expect_identical(compiled_from_cran[1:2], c("Depends", "Imports"))
  # a binary from Posit's package manager or a Linux binary repository is not compiled here
  expect_false("LinkingTo" %in% strong_for("pkg", db("https://p3m.dev/cran/__linux__/noble/latest/src/contrib", "yes")))
  expect_false("LinkingTo" %in% strong_for("pkg", db("https://cran.r-project.org/bin/linux/ubuntu/src/contrib", "yes")))
  # nor is one that needs no compilation
  expect_false("LinkingTo" %in% strong_for("pkg", db("https://cran.r-project.org/src/contrib", "no")))
})
