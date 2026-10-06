# hera as Elara loads it (native/src/elara/r/interpreter_r.cpp): every R/*.R evaluated, in sorted order, into one
# environment attached as "tools:jovian", the NAMESPACE's S3 methods registered, .jv.onLoad() run, the environment
# locked. The tests then call hera's functions as the kernel does, without a kernel: what needs Elara's routines
# (.Call into "(embedding)": JSON, display, output) is not tested here, it is the integration tests' part.
hera_dir <- Sys.getenv("HERA_DIR", unset = normalizePath(file.path("..", ".."), mustWork = FALSE))
if (!file.exists(file.path(hera_dir, "DESCRIPTION"))) stop("hera not found at ", hera_dir, ": set HERA_DIR")

if (!"tools:jovian" %in% search()) {
  files <- sort(list.files(file.path(hera_dir, "R"), pattern = "[.]R$", full.names = TRUE))
  version <- read.dcf(file.path(hera_dir, "DESCRIPTION"), fields = "Version")[1, 1]
  directives <- readLines(file.path(hera_dir, "NAMESPACE"), warn = FALSE)
  methods <- regmatches(directives, regexec("^S3method\\(([^,)]+),([^,)]+),([^,)]+)\\)", directives))
  methods <- lapply(Filter(length, methods), function(m) gsub("[\"`]", "", m[-1]))

  env <- attach(NULL, pos = 2L, name = "tools:jovian")
  for (file in files) {
    text <- gsub("\r", "", paste(readLines(file, warn = FALSE, encoding = "UTF-8"), collapse = "\n"), fixed = TRUE)
    for (e in parse(text = text, keep.source = FALSE, encoding = "UTF-8")) eval(e, env)
  }
  assign(".elara.version", unname(version), envir = env)
  plain <- grep("^[^.]", ls(env, all.names = TRUE), value = TRUE)
  if (length(plain)) stop("names in tools:jovian without a dot: ", paste(plain, collapse = ", "))
  for (m in methods) registerS3method(m[[1]], m[[2]], get(m[[3]], envir = env), envir = baseenv())
  get(".jv.onLoad", envir = env)(NULL, "elara")
  # hera's log lines go to Elara's log directly (.Call into "(embedding)"); with no Elara, they go nowhere
  assign(".jv.log.debug", function(...) invisible(NULL), envir = env)
  assign(".jv.log.warning", function(...) invisible(NULL), envir = env)
  lockEnvironment(env, bindings = TRUE)
}

jovian <- as.environment("tools:jovian")

# A function of hera's, by name, called as hera's own code calls it: from inside tools:jovian. hera's generics
# (.elara.mime_types, .elara.mime_bundle) find their methods beside them, through the caller's environment, not by
# registration, so a call from a test's environment would find none.
jv <- function(name) {
  fn <- get(name, envir = jovian, inherits = FALSE)
  function(...) eval(as.call(c(list(fn), list(...))), envir = new.env(parent = jovian))
}

# An object in the global environment for the length of a test (hera's variables functions read the global environment)
with_global <- function(name, value, code) {
  assign(name, value, envir = globalenv())
  on.exit(rm(list = name, envir = globalenv()), add = TRUE)
  force(code)
}
