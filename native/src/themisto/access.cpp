#include "access.hpp"

#include <cctype>
#include <stdexcept>

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace themisto { namespace access {

    std::string newToken()
    {
        unsigned char bytes[32];
        if (RAND_bytes(bytes, sizeof(bytes)) != 1)
        {
            throw std::runtime_error("could not generate the supervisor's access token (RAND_bytes failed)");
        }
        static const char* hex = "0123456789abcdef";
        std::string token;
        token.reserve(sizeof(bytes) * 2);
        for (unsigned char b : bytes)
        {
            token += hex[b >> 4];
            token += hex[b & 0x0f];
        }
        return token;
    }

    bool tokenMatches(const std::string& presented, const std::string& token)
    {
        if (token.empty() || presented.size() != token.size())
        {
            return false;
        }
        return CRYPTO_memcmp(presented.data(), token.data(), token.size()) == 0;
    }

    std::optional<std::string> bearerToken(const std::string& authorization)
    {
        const std::string scheme = "bearer ";
        if (authorization.size() <= scheme.size())
        {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < scheme.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(authorization[i])) != scheme[i])
            {
                return std::nullopt;
            }
        }
        std::string value = authorization.substr(scheme.size());
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        while (!value.empty() && value.back() == ' ') value.pop_back();
        if (value.empty()) return std::nullopt;
        return value;
    }

    namespace
    {
        int hexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::string percentDecode(const std::string& text)
        {
            std::string out;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0)
                {
                    out += static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
                    i += 2;
                }
                else if (text[i] == '+')
                {
                    out += ' ';
                }
                else
                {
                    out += text[i];
                }
            }
            return out;
        }
    }

    std::optional<std::string> queryParameter(const std::string& uri, const std::string& name)
    {
        std::size_t question = uri.find('?');
        if (question == std::string::npos) return std::nullopt;
        std::string query = uri.substr(question + 1);
        std::size_t hash = query.find('#');
        if (hash != std::string::npos) query.resize(hash);

        std::size_t start = 0;
        while (start <= query.size())
        {
            std::size_t end = query.find('&', start);
            if (end == std::string::npos) end = query.size();
            std::string pair = query.substr(start, end - start);
            std::size_t equals = pair.find('=');
            std::string key = percentDecode(pair.substr(0, equals));
            if (key == name)
            {
                return equals == std::string::npos ? std::string() : percentDecode(pair.substr(equals + 1));
            }
            start = end + 1;
        }
        return std::nullopt;
    }

    std::string pathOf(const std::string& uri)
    {
        std::size_t cut = uri.find_first_of("?#");
        return cut == std::string::npos ? uri : uri.substr(0, cut);
    }

    bool fromBrowser(const std::string& origin)
    {
        return !origin.empty();
    }

    Verdict check(const std::string& origin, const std::string& authorization, const std::string& uri, const std::string& token)
    {
        if (fromBrowser(origin)) return Verdict::FromBrowser;
        if (token.empty()) return Verdict::Allowed;

        if (auto bearer = bearerToken(authorization); bearer && tokenMatches(*bearer, token))
        {
            return Verdict::Allowed;
        }
        if (auto fromQuery = queryParameter(uri, "token"); fromQuery && tokenMatches(*fromQuery, token))
        {
            return Verdict::Allowed;
        }
        return Verdict::BadToken;
    }

} }
