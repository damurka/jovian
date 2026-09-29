# hera (development version)

* Printing a lot of output is up to about four times faster: it is no longer also written to a temporary file and read back after every expression (about 150 ms per MB), only to be discarded.
* Every dependency now has a minimum version.
* Calls into Elara (every message, warning and stderr write) no longer re-check that R runs inside Elara each time; a cell calling `message()` 25 000 times ran several times slower because of it.

* Output from a single long-running expression (a loop of print() calls) now streams as it is produced instead of arriving when the expression finishes.

# hera 0.1.1

* Initial CRAN submission.
