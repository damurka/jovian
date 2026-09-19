#ifndef ADRASTEA_JSON_HPP
#define ADRASTEA_JSON_HPP

// Remove annoying false positive warning on GCC: json.hpp:1394:23: warning: potential null pointer dereference
// See https://github.com/nlohmann/json/issues/3525
#ifdef __GNUC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wnull-dereference"
#   include <nlohmann/json.hpp>
#   pragma GCC diagnostic pop
#else
#   include <nlohmann/json.hpp>
#endif

namespace adrastea {
    // Alias for convenience across the adrastea namespace
    using json = nlohmann::json;
    namespace nl = nlohmann;
}

#endif