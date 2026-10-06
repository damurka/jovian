# A cell's code parsed with the cell's name, and an error's report (R/repl.R).

test_that("a cell parses with its execution count as its file name", {
  parse_cell <- jv(".jv.repl.parse")
  exprs <- parse_cell("x <- 1\ny", 7L)
  expect_type(exprs, "expression")
  expect_length(exprs, 2L)
  srcref <- attr(exprs, "srcref")[[1L]]
  expect_identical(attr(srcref, "srcfile")$filename, "[7]")
})

test_that("a cell that does not parse gives the parser's message", {
  message <- jv(".jv.repl.parse")("1 +", 1L)
  expect_type(message, "character")
  expect_match(message, "unexpected")
})

test_that("an error's report has the message and the calls, in red headings", {
  red <- jv(".jv.errors.red")
  expect_identical(red("x"), "\033[31mx\033[39m")

  report <- jv(".jv.errors.report")(simpleError("boom"), list())
  expect_identical(report$evalue, "boom")
  expect_identical(report$traceback[[1L]], red("--- Error"))
  expect_identical(report$traceback[[2L]], "boom")
  expect_identical(report$traceback[[4L]], red("--- Traceback (most recent call last)"))

  with_call <- jv(".jv.errors.report")(simpleError("bad", call = quote(f(1))), list())
  expect_true(any(grepl("f(1)", with_call$traceback, fixed = TRUE)))
})
