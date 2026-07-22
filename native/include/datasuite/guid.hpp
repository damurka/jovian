#ifndef DATASUITE_GUID_HPP
#define DATASUITE_GUID_HPP

#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
    struct alignas(64) Guid {
        std::array<char, 64> buffer{};

        // 1. Default constructor
        Guid() = default;

        // 2. Creatable from a std::string
        Guid(const std::string& str) {
            std::size_t len = std::min(str.length(), (std::size_t)63);
            std::copy(str.begin(), str.begin() + len, buffer.begin());
            buffer[len] = '\0';
        }

        // 3. Creatable from a const char* (string literal)
        Guid(const char* str) {
            if (str) {
                std::size_t len = std::min(std::strlen(str), (std::size_t)63);
                std::copy(str, str + len, buffer.begin());
                buffer[len] = '\0';
            }
        }

        // 4. Readable AS a std::string (Implicit conversion operator)
        operator std::string() const {
            return std::string(buffer.data());
        }

        // 5. Explicit string conversion
        std::string to_string() const {
            return std::string(buffer.data());
        }

        // 6. Required for std::map (Allows sorting)
        bool operator<(const Guid& other) const {
            return std::string_view(buffer.data()) < std::string_view(other.buffer.data());
        }

        // 7. Required for equality checks
        bool operator==(const Guid& other) const {
            return std::string_view(buffer.data()) == std::string_view(other.buffer.data());
        }

        char* data() { return buffer.data(); }
        const char* data() const { return buffer.data(); }
    };

    // 8. The nlohmann::json magic! (Serialization)
    // By defining this in the same namespace, nlohmann will automatically find it.
    inline void to_json(json& j, const Guid& g) {
        j = g.to_string();
    }

    // 9. The nlohmann::json magic! (Deserialization)
    inline void from_json(const json& j, Guid& g) {
        g = Guid(j.get<std::string>());
    }


    DATASUITE_API Guid new_guid();
}

#endif
