# The package and help RPCs a frontend calls (R/packages.R, R/help.R, R/rpc.R), apart from the JSON around them.

test_that("whether packages are installed, at a version", {
  is_installed <- jv(".jv.rpc.is_installed")
  rows <- is_installed(list("stats", "no.such.package.zz"), list(stats = "1.0"))
  expect_s3_class(rows, "data.frame")
  expect_identical(rows$name, c("stats", "no.such.package.zz"))
  expect_identical(rows$installed, c(TRUE, FALSE))
  expect_false(is.na(rows$version[[1L]]))
  expect_true(is.na(rows$version[[2L]]))
  expect_false(is_installed(list("stats"), list(stats = "999.0"))$installed)
  expect_identical(is_installed(list()), list())
})

test_that("the repositories are those given, then the session's, with no trailing slash", {
  repos <- jv(".jv.rpc.repos")
  session <- repos(NULL)
  expect_true(any(grepl("^https://", session)))
  given <- repos(list("https://me.r-universe.dev/"))
  expect_identical(given[[1L]], "https://me.r-universe.dev")
  expect_true(all(session %in% given))
  expect_identical(anyDuplicated(repos(list(session[[1L]]))), 0L)
})

test_that("a data frame as rows has no row names", {
  rows <- jv(".jv.rpc.rows")(data.frame(a = 1:2, row.names = c("x", "y")))
  expect_true(.row_names_info(rows) < 0L)
  expect_identical(rows$a, 1:2)
})

test_that("the help server's address for a topic, or NULL", {
  help_url <- jv(".jv.rpc.help_url")
  url <- help_url("mean", "base")
  expect_match(url, "^http://127[.]0[.]0[.]1:[0-9]+/library/base/html/mean[.]html$")
  expect_null(help_url("zzqq_no_such_topic"))
  server <- jv(".jv.rpc.help_server")()
  expect_true(server$port > 0L)
  expect_identical(server$url, sprintf("http://127.0.0.1:%d", server$port))
})
