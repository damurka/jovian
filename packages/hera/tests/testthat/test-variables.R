# The session's variables for a variables pane and a data viewer (R/variables.R): sizes, previews, cells as text, a
# page of a table, and the data environments' tables.

test_that("an object's size is said as a frontend shows it", {
  size <- jv(".jv.vars.size")
  expect_identical(size(matrix(1:6, 3L)), "3 × 2")
  expect_identical(size(mtcars), "32 × 11")
  expect_identical(size(1:1000), "1,000")
  expect_identical(size(list(1, 2)), "2")
  expect_identical(size(function(x) x), "")
  expect_identical(size(NULL), "")
  expect_identical(size(list2env(list(a = 1, b = 2))), "2 objects")
})

test_that("a preview is one line of what an object holds", {
  preview <- jv(".jv.vars.preview")
  expect_identical(preview(c(1.5, NA, 3)), "1.5 NA 3.0")
  expect_identical(preview(c("a", NA)), "\"a\" NA")
  expect_identical(preview(factor(c("x", "y"))), "\"x\" \"y\"")
  expect_identical(preview(mtcars), "32 obs. of 11 variables")
  expect_identical(preview(function(x, ...) x), "function(x, ...)")
  expect_identical(preview(list(1, 2)), "List of 2")
  expect_identical(preview(1:100), paste(1:20, collapse = " "))
  long <- preview(strrep("a", 300L))
  expect_identical(nchar(long), 203L)
  expect_true(endsWith(long, "..."))
})

test_that("a table is a data frame or a matrix", {
  is_table <- jv(".jv.vars.is_table")
  expect_true(is_table(mtcars))
  expect_true(is_table(matrix(1:4, 2L)))
  expect_false(is_table(array(1:8, c(2L, 2L, 2L))))
  expect_false(is_table(1:3))
  expect_false(is_table(list(a = 1)))
})

test_that("cells are text as R prints them", {
  format_cells <- jv(".jv.vars.format")
  expect_identical(format_cells(factor(c("a", NA))), c("a", "NA"))
  expect_identical(format_cells(as.Date(c("2024-01-02", NA))), c("2024-01-02", "NA"))
  expect_identical(format_cells(c(1.5, NA)), c("1.5", "NA"))
  expect_identical(format_cells(list(1, NULL, "x")), c("1", "NULL", "\"x\""))
  expect_identical(format_cells(character()), character())
  expect_identical(nchar(format_cells(strrep("b", 1500L))), 1000L)
})

test_that("a column that is itself a matrix gives each row as one value", {
  rows <- jv(".jv.vars.rows")
  expect_identical(rows(matrix(1:4, 2L), 1:2), c("1, 3", "2, 4"))
  expect_identical(rows(c("p", "q", "r"), 2:3), c("q", "r"))
})

test_that("a page of a table has its columns, its rows as text, and row names when they are names", {
  var_table <- jv(".jv.rpc.var_table")
  with_global("hera_test_df", data.frame(a = 1:3, b = c("x", "y", "z"), stringsAsFactors = FALSE), {
    page <- var_table("hera_test_df", 2L, 5L)
    expect_identical(page$name, "hera_test_df")
    expect_identical(page$rowCount, 3L)
    expect_identical(page$columns, list(list(name = "a", type = "integer"), list(name = "b", type = "character")))
    expect_identical(page$start, 2L)
    expect_identical(page$count, 2L)
    expect_null(page$rowLabels)
    expect_identical(page$rows, list(list("2", "y"), list("3", "z")))
    expect_identical(var_table("hera_test_df", 10L, 5L)$count, 0L)
  })
  with_global("hera_test_cars", mtcars, {
    page <- var_table("hera_test_cars", 1L, 2L)
    expect_identical(page$rowLabels, list("Mazda RX4", "Mazda RX4 Wag"))
    expect_identical(page$rows[[1L]][[1L]], "21")
  })
  with_global("hera_test_matrix", matrix(1:4, 2L), {
    page <- var_table("hera_test_matrix")
    expect_identical(vapply(page$columns, `[[`, "", "name"), c("V1", "V2"))
    expect_identical(page$rows, list(list("1", "3"), list("2", "4")))
  })
})

test_that("a page of what is not there, or not a table, is an error that says so", {
  var_table <- jv(".jv.rpc.var_table")
  expect_error(var_table("hera_no_such_object_zz"), "no object hera_no_such_object_zz")
  with_global("hera_test_vector", 1:3, expect_error(var_table("hera_test_vector"), "not a data frame or a matrix"))
})

test_that("the variables list describes the global environment's objects", {
  var_list <- jv(".jv.rpc.var_list")
  with_global("hera_test_df", data.frame(a = 1:3, b = 4:6), {
    with_global("hera_test_fn", function(x, y) x, {
      listed <- var_list()
      names <- vapply(listed, `[[`, "", "name")
      df <- listed[[match("hera_test_df", names)]]
      expect_identical(df[c("type", "size", "summary", "table")], list(type = "data.frame", size = "3 × 2", summary = "3 obs. of 2 variables", table = TRUE))
      fn <- listed[[match("hera_test_fn", names)]]
      expect_identical(fn[c("type", "summary", "table")], list(type = "function", summary = "function(x, y)", table = FALSE))
    })
  })
})

test_that("an attached data environment's tables are listed by name and read when shown", {
  data_env <- attach(NULL, name = "hera:test-data")
  on.exit(detach("hera:test-data"), add = TRUE)
  attr(data_env, "jovian.tables") <- TRUE
  assign("hera_remote_table", data.frame(z = c(10, 20)), envir = data_env)

  listed <- jv(".jv.rpc.var_list")()
  entry <- Filter(function(v) identical(v$name, "hera_remote_table"), listed)
  expect_length(entry, 1L)
  expect_identical(entry[[1L]]$summary, "read when shown, from hera:test-data")
  expect_true(entry[[1L]]$table)

  page <- jv(".jv.rpc.var_table")("hera_remote_table")
  expect_identical(page$rowCount, 2L)
  expect_identical(page$rows, list(list("10"), list("20")))
})
