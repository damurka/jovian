#ifndef THEMISTO_ACCESS_HPP
#define THEMISTO_ACCESS_HPP

// Who may use the supervisor's HTTP API and WebSocket relay.
//
// Both listen on 127.0.0.1, which keeps other machines out but not other
// programs on this one -- and the API runs code (an execute frame) and loads
// arbitrary shared libraries (rHome / pythonHome / stataHome), so reaching it
// is as good as a shell. Every request must therefore carry the token the
// supervisor printed in its ready line (only its parent, which reads that
// line from a pipe, knows it), and nothing sent by a web browser is accepted:
// a page the user visits can reach 127.0.0.1 too, and browsers always send an
// Origin header, which Node's fetch and WebSocket do not.

#include <optional>
#include <string>

namespace themisto { namespace access {

    // A new random token: 32 bytes from OpenSSL's CSPRNG, as 64 hex digits.
    std::string newToken();

    // Whether `presented` equals `token`, in time independent of where they
    // differ. An empty token never matches.
    bool tokenMatches(const std::string& presented, const std::string& token);

    // The token in an "Authorization: Bearer <token>" header value, if any.
    std::optional<std::string> bearerToken(const std::string& authorization);

    // The value of `name` in a URI's query string ("/x?token=abc" -> "abc"),
    // percent-decoded, if present.
    std::optional<std::string> queryParameter(const std::string& uri, const std::string& name);

    // The URI without its query string.
    std::string pathOf(const std::string& uri);

    // Whether a request with this Origin header value (empty when there was
    // none) comes from a web page. Any Origin at all does: Node sends none.
    bool fromBrowser(const std::string& origin);

    enum class Verdict
    {
        Allowed,
        FromBrowser,   // 403
        BadToken,      // 401
    };

    // The decision for one request: its Origin header, its Authorization
    // header, and its URI (for a ?token= query parameter -- WebSocket clients
    // in Node cannot set headers). An empty `token` disables the check (tests
    // and --no-auth only).
    Verdict check(const std::string& origin, const std::string& authorization, const std::string& uri, const std::string& token);

} }

#endif
