#ifndef ELARA_R_JSON_CONVERT_HPP
#define ELARA_R_JSON_CONVERT_HPP

// R values to and from JSON, with jsonlite's rules (see the comment above sexpToJson() in routine.cpp): what hera
// hands the kernel -- display data, a cell's result, an inspect reply -- arrives as R values and is converted once,
// here, rather than written as JSON text in R and parsed back.

#include "elara/r/r_dynlib.hpp"
#include "adrastea/json.hpp"

namespace elara
{
    namespace routines
    {
        // `nullAsObject`: what NULL becomes -- {} (jsonlite's default) or null.
        adrastea::json sexpToJson(SEXP x, bool nullAsObject);

        SEXP jsonToSexp(const adrastea::json &j);

        // A value hera hands over: an R list, converted (NULL as {}); or JSON text, parsed, as earlier hera versions
        // sent it.
        adrastea::json jsonFromR(SEXP x);
    }
}

#endif // ELARA_R_JSON_CONVERT_HPP
