# R/utils.R and the pure part of R/routines.R (the JSON itself is written and read by Elara).

test_that("HTML is escaped", {
  expect_identical(jv(".jv.utils.html_escape")("<a & b>"), "&lt;a &amp; b&gt;")
  expect_identical(jv(".jv.utils.html_escape")("plain"), "plain")
})

test_that("a named empty list has names", {
  x <- jv(".jv.utils.named_list")()
  expect_identical(x, structure(list(), names = character()))
  expect_identical(names(x), character())
})

test_that("the operating system is one of the three", {
  expect_true(jv(".jv.utils.os")() %in% c("win", "osx", "unix"))
})

test_that("what Elara cannot format becomes text before it is written as JSON", {
  prepare <- jv(".jv.json.prepare")
  expect_identical(prepare(as.Date("2024-01-02")), "2024-01-02")
  expect_identical(prepare(as.POSIXct("2024-01-02 03:04:05", tz = "UTC")), "2024-01-02 03:04:05")
  expect_identical(prepare(list(d = as.Date("2024-01-02"), n = 1, inner = list(when = as.Date("2024-02-03")))),
    list(d = "2024-01-02", n = 1, inner = list(when = "2024-02-03")))
  # left to Elara: plain vectors, factors, I() and JSON text
  expect_identical(prepare(c(1, 2)), c(1, 2))
  expect_identical(prepare(factor("a")), factor("a"))
  expect_identical(prepare(I("x")), I("x"))
  expect_identical(prepare(structure("{}", class = "json")), structure("{}", class = "json"))
  expect_null(prepare(NULL))
  expect_identical(prepare(list()), list())
})

test_that("hera's version is the DESCRIPTION's, and every name is dot-named", {
  expect_identical(get(".elara.version", envir = jovian), unname(read.dcf(file.path(hera_dir, "DESCRIPTION"), fields = "Version")[1, 1]))
  expect_identical(grep("^[^.]", ls(jovian, all.names = TRUE), value = TRUE), character())
})
