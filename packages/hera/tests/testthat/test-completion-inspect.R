# Completion and inspection, as a notebook asks for them (R/completion.R, R/inspect.R).

test_that("completion offers the names that start with the token, and says where the token is", {
  complete <- jv(".elara.complete")
  res <- complete("mea", 3L)
  expect_true("mean" %in% res[[1L]])
  expect_equal(res[[2L]], c(0, 3))
  # on the second line of a cell the positions count from the cell's start
  res <- complete("x <- 1\nmea", 10L)
  expect_true("mean" %in% res[[1L]])
  expect_equal(res[[2L]], c(7, 10))
  # a package's exports after ::
  expect_true("stats::sd" %in% complete("stats::sd", 9L)[[1L]])
})

test_that("completing an empty cell is not an error", {
  res <- jv(".elara.complete")("", 0L)
  expect_length(res, 2L)
})

test_that("inspecting a function gives its documentation, in text and HTML", {
  inspect <- jv(".jv.inspect.request")
  res <- inspect("mean(x)", 1L)
  expect_true(res$found)
  expect_match(res$data[["text/plain"]], "Arithmetic Mean", fixed = TRUE)
  expect_match(res$data[["text/html"]], "<h2>Arithmetic Mean</h2>", fixed = TRUE)
})

test_that("inspecting a reserved word gives its help page", {
  res <- jv(".jv.inspect.request")("if (x) 1", 1L)
  expect_true(res$found)
  expect_match(res$data[["text/plain"]], "Control Flow", fixed = TRUE)
  res <- jv(".jv.inspect.request")("function(x) x", 3L)
  expect_true(res$found)
})

test_that("inspecting nothing finds nothing", {
  res <- jv(".jv.inspect.request")("zzqq_no_such_thing", 1L)
  expect_false(res$found)
})
