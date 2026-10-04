# R's help, for a frontend's help pane (Jovian's Session.helpServer(), helpUrl()). Called through .jv.rpc.call() (rpc.R).

# R's own help server (tools::startDynamicHelp()), started if need be: its port and address. It answers while the
# session is idle (Elara services R's events then, as R's console does).
.jv.rpc.help_server <- function() {
  port <- tools::startDynamicHelp(NA)
  if (!port) port <- suppressMessages(tools::startDynamicHelp(TRUE))
  list(port = port, url = sprintf("http://127.0.0.1:%d", port))
}

# The help server's address for a help topic (in `package`, else wherever it is found), or NULL
.jv.rpc.help_url <- function(topic, package = NULL) {
  paths <- as.character(if (is.null(package)) utils::help(topic, help_type = "html") else utils::help(topic, package = (package), help_type = "html"))
  if (!length(paths)) return(NULL)
  server <- .jv.rpc.help_server()
  path <- paths[[1L]]
  sprintf("%s/library/%s/html/%s.html", server$url, basename(dirname(dirname(path))), basename(path))
}
