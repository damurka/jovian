# What a value is shown as (R/mime_bundle.R): base R's text for anything, and more through IRdisplay when it is there.

test_that("a plain value is shown as the text print() writes", {
  expect_identical(jv(".elara.mime_types")(1:3), "text/plain")
  bundle <- jv(".elara.mime_bundle")(1:3)
  expect_identical(bundle$data[["text/plain"]], "[1] 1 2 3")
  expect_null(bundle$metadata)
})

test_that("a help page is shown as text, and as HTML", {
  page <- utils::help("mean", package = "base")
  types <- jv(".elara.mime_types")(page)
  expect_true(all(c("text/plain", "text/html") %in% types))
  bundle <- jv(".elara.mime_bundle")(page)
  expect_match(bundle$data[["text/plain"]], "Arithmetic Mean", fixed = TRUE)
  expect_match(bundle$data[["text/html"]], "<", fixed = TRUE)
})

test_that("a data frame is shown through IRdisplay when it is installed", {
  skip_if_not_installed("IRdisplay")
  bundle <- jv(".elara.mime_bundle")(data.frame(a = 1:2), mimetypes = c("text/plain", "text/html"))
  expect_match(bundle$data[["text/plain"]], "a", fixed = TRUE)
  expect_match(bundle$data[["text/html"]], "<table", fixed = TRUE)
})
