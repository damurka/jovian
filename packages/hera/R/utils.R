# borrowed from IRkernel
.jv.utils.os <- function() {
    switch(.Platform$OS.type,
        windows = 'win',
        unix = if (identical(Sys.info()[['sysname']], 'Darwin')) 'osx'
               else 'unix'
    )
}

.jv.utils.html_escape <- function(text) {
    text <- gsub("&", "&amp;", text, fixed = TRUE)
    text <- gsub("<", "&lt;", text, fixed = TRUE)
    gsub(">", "&gt;", text, fixed = TRUE)
}

.jv.utils.named_list <- function() {
    `names<-`(list(), character())
}
