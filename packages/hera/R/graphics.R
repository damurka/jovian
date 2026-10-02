# Plots. One device for the whole session, as R's console has its plot window: a cell can add to the previous cell's
# plot (abline() after plot()), and par() settings last. R opens it when code first draws (options(device), set at
# start-up), so a session that never plots loads nothing for it. It records without drawing pixels -- ragg's
# agg_record() when ragg is installed, as Ark does (its text metrics are those of agg_png(), which then draws the
# image); else a png() that writes nowhere -- so a plot is drawn once, into the image sent. After each top-level
# expression Elara asks whether the plot changed (.jv.graphics.after_expression()); a changed plot is recorded and,
# once complete, sent. A cell's last plot is sent when the cell is done (.jv.graphics.cell_done()).

# The defaults of the repr.plot.* options (those of repr, which used to draw the plots).
.jv.graphics.option <- function(name) {
  value <- getOption(paste0("repr.plot.", name))
  if (!is.null(value)) return(value)
  switch(name, width = 7, height = 7, res = 120, pointsize = 12, bg = "white", quality = 90, family = "sans")
}

# Whether ragg (with agg_record(), ragg >= 1.4.0) records and draws the plots. options(jupyter.plot_ragg = FALSE) uses
# R's png() instead: ragg draws no text in fonts that carry bitmap versions of their letters (Calibri, Cambria) at some
# sizes (12 to 19 pixels: 8 to 10 points at the default 120 dpi).
.jv.graphics.ragg <- function() {
  if (isFALSE(getOption("jupyter.plot_ragg", TRUE))) return(FALSE)
  if (is.null(.jv.the$ragg)) {
    .jv.the$ragg <- requireNamespace("ragg", quietly = TRUE) && exists("agg_record", envir = asNamespace("ragg"), inherits = FALSE)
  }
  isTRUE(.jv.the$ragg)
}

# Opens the session's device (R calls this, through options(device), when code draws with no device open): as large
# as the images it is drawn into, so what R code measures on it (strwidth(), for a legend) is what the image shows
.jv.graphics.open_device <- function(...) {
  width <- .jv.graphics.option("width")
  height <- .jv.graphics.option("height")
  res <- .jv.graphics.option("res")
  if (.jv.graphics.ragg()) {
    ragg::agg_record(width = width, height = height, units = "in", res = res,
      pointsize = .jv.graphics.option("pointsize"), background = .jv.graphics.option("bg"))
  } else {
    # macOS's Quartz png() cannot write to /dev/null (R 4.5: "QuartzBitmap_Output - unable to open file"): a file in
    # the session's temporary directory there, overwritten by each page
    nowhere <- switch(.jv.utils.os(), win = "NUL", osx = file.path(tempdir(), "jv-device.png"), "/dev/null")
    grDevices::png(nowhere, width = width, height = height, units = "in",
      res = res, pointsize = .jv.graphics.option("pointsize"), bg = .jv.graphics.option("bg"))
  }
  .jv.the$device <- grDevices::dev.cur()
  grDevices::dev.control(displaylist = "enable")
  .jv.the$last_snapshot <- NULL
  .jv.the$last_display_list <- NULL
  invisible()
}

# The session's device, if code has drawn yet
.jv.graphics.device <- function() {
  if (!is.null(.jv.the$device) && !(.jv.the$device %in% grDevices::dev.list())) .jv.the$device <- NULL
  .jv.the$device
}

# Display list entries that change no pixel: setting graphical parameters, a layout, the palette.
.jv.graphics.NON_VISUAL <- c("C_par", "C_layout", "palette", "palette2")

.jv.graphics.is_visual <- function(entry) {
  name <- tryCatch(entry[[2]][[1]]$name, error = function(e) NULL)
  !is.character(name) || !(name %in% .jv.graphics.NON_VISUAL)
}

# Whether two display lists draw the same: a par() or layout() call added to a plot (a later cell's
# par(mfrow = ...) lands on the last cell's plot) is no new plot.
.jv.graphics.draws_same <- function(old, new) {
  identical(old, new) || identical(Filter(.jv.graphics.is_visual, old), Filter(.jv.graphics.is_visual, new))
}

# Whether `current` only adds to `previous` (a line drawn on it): then only the result is sent (from IRkernel).
.jv.graphics.builds_upon <- function(previous, current) {
  if (is.null(previous)) return(TRUE)
  lprev <- length(previous[[1]])
  lcurrent <- length(current[[1]])
  lcurrent >= lprev && identical(current[[1]][seq_len(lprev)], previous[[1]][seq_len(lprev)])
}

# The session's plot when it has changed since the last look: once its page is complete (after the last panel of a
# par(mfrow) layout, say), or whatever there is at the end of the cell (`incomplete`).
.jv.graphics.snapshot <- function(incomplete = FALSE) {
  device <- .jv.the$device
  if (is.null(device) || !(device %in% grDevices::dev.list())) return(NULL)
  if (grDevices::dev.cur() != device) return(NULL)
  if (!incomplete && !graphics::par("page")) return(NULL)
  # nothing drawn since the last look: no need to record the plot (a copy of all it draws)
  shown <- .Call("elara_display_list_id", PACKAGE = "(embedding)")
  if (identical(shown, .jv.the$last_display_list)) return(NULL)
  # a new page (plot.new() starts a new display list) is a new plot even when it draws the same as the last
  new_page <- is.null(.jv.the$last_display_list) || !identical(sub(":.*", "", shown), sub(":.*", "", .jv.the$last_display_list))
  .jv.the$last_display_list <- shown
  plot <- grDevices::recordPlot()
  if (!length(plot[[1]]) || !any(vapply(plot[[1]], .jv.graphics.is_visual, logical(1)))) return(NULL)
  if (!new_page && !is.null(.jv.the$last_snapshot) && .jv.graphics.draws_same(.jv.the$last_snapshot[[1]], plot[[1]])) return(NULL)
  .jv.the$last_snapshot <- plot
  plot
}

# A changed plot: the cell's previous one is sent unless this one only adds to it.
.jv.graphics.changed <- function(plot) {
  attr(plot, ".irkernel_width") <- .jv.graphics.option("width")
  attr(plot, ".irkernel_height") <- .jv.graphics.option("height")
  attr(plot, ".irkernel_res") <- .jv.graphics.option("res")
  attr(plot, ".irkernel_ppi") <- attr(plot, ".irkernel_res") / getOption("jupyter.plot_scale", 2)
  if (!.jv.graphics.builds_upon(.jv.the$cell_plot, plot)) .jv.graphics.send(.jv.the$cell_plot)
  .jv.the$cell_plot <- plot
}

.jv.graphics.after_expression <- function() {
  plot <- .jv.graphics.snapshot()
  if (!is.null(plot)) .jv.graphics.changed(plot)
  invisible()
}

.jv.graphics.cell_done <- function() {
  plot <- .jv.graphics.snapshot(incomplete = TRUE)
  if (!is.null(plot)) .jv.graphics.changed(plot)
  .jv.graphics.send(.jv.the$cell_plot)
  .jv.the$cell_plot <- NULL
  invisible()
}

# A recorded plot drawn on a file device of one of the plot mime types: its bytes (text for SVG), or NULL for a type
# that isn't drawn.
.jv.graphics.render <- function(plot, mime, width, height, res) {
  if (identical(mime, "text/plain")) return("plot without title")
  ext <- switch(mime, "image/png" = ".png", "image/jpeg" = ".jpeg", "image/svg+xml" = ".svg", "application/pdf" = ".pdf", NULL)
  if (is.null(ext)) return(NULL)

  file <- tempfile(fileext = ext)
  on.exit(unlink(file), add = TRUE)
  pointsize <- .jv.graphics.option("pointsize")
  bg <- .jv.graphics.option("bg")
  ragg <- .jv.graphics.ragg()
  switch(mime,
    "image/png" = if (ragg) {
      ragg::agg_png(file, width = width, height = height, units = "in", res = res, pointsize = pointsize, background = bg)
    } else {
      grDevices::png(file, width = width, height = height, units = "in", res = res, pointsize = pointsize, bg = bg)
    },
    "image/jpeg" = if (ragg) {
      ragg::agg_jpeg(file, width = width, height = height, units = "in", res = res, pointsize = pointsize, background = bg,
        quality = .jv.graphics.option("quality"))
    } else {
      grDevices::jpeg(file, width = width, height = height, units = "in", res = res, pointsize = pointsize, bg = bg,
        quality = .jv.graphics.option("quality"))
    },
    "image/svg+xml" = grDevices::svg(file, width = width, height = height, pointsize = pointsize, bg = bg, family = .jv.graphics.option("family")),
    "application/pdf" = grDevices::pdf(file, width = width, height = height, pointsize = pointsize, bg = bg)
  )
  device <- grDevices::dev.cur()
  tryCatch(grDevices::replayPlot(plot), finally = grDevices::dev.off(device))
  # replaying made the file device current; the session's device is again
  if (!is.null(.jv.the$device) && .jv.the$device %in% grDevices::dev.list()) grDevices::dev.set(.jv.the$device)

  size <- file.info(file)$size
  if (identical(mime, "image/svg+xml")) readChar(file, size, useBytes = TRUE) else readBin(file, "raw", size)
}

.jv.graphics.send <- function(plot) {
  if (is.null(plot)) return(invisible())
  w <- attr(plot, ".irkernel_width")
  h <- attr(plot, ".irkernel_height")
  res <- attr(plot, ".irkernel_res")
  ppi <- attr(plot, ".irkernel_ppi")

  data <- .jv.utils.named_list()
  metadata <- .jv.utils.named_list()
  for (mime in getOption("jupyter.plot_mimetypes")) {
    rendered <- .jv.graphics.render(plot, mime, w, h, res)
    if (is.null(rendered)) next
    data[[mime]] <- rendered
    if (!identical(mime, "text/plain")) metadata[[mime]] <- list(width = w * ppi, height = h * ppi)
    # Isolating SVGs (putting them in an iframe) avoids strange interactions with CSS on the page.
    if (identical(mime, "image/svg+xml")) metadata[[mime]]$isolated <- TRUE
  }
  .elara.display_data(data, metadata)
}
