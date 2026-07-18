#ifndef DATASUITE_GUID_HPP
#define DATASUITE_GUID_HPP

#include "nlohmann/json.hpp"
#include "datasuite.hpp"

namespace datasuite
{
    struct alignas(64) guid {
        std::array<char, 64> buffer{};

        // 1. Default constructor
        guid() = default;

        // 2. Creatable from a std::string
        guid(const std::string& str) {
            std::size_t len = std::min(str.length(), (std::size_t)63);
            std::copy(str.begin(), str.begin() + len, buffer.begin());
            buffer[len] = '\0';
        }

        // 3. Creatable from a const char* (string literal)
        guid(const char* str) {
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
        bool operator<(const guid& other) const {
            return std::string_view(buffer.data()) < std::string_view(other.buffer.data());
        }

        // 7. Required for equality checks
        bool operator==(const guid& other) const {
            return std::string_view(buffer.data()) == std::string_view(other.buffer.data());
        }

        char* data() { return buffer.data(); }
        const char* data() const { return buffer.data(); }
    };

    // 8. The nlohmann::json magic! (Serialization)
    // By defining this in the same namespace, nlohmann will automatically find it.
    inline void to_json(nlohmann::json& j, const guid& g) {
        j = g.to_string();
    }

    // 9. The nlohmann::json magic! (Deserialization)
    inline void from_json(const nlohmann::json& j, guid& g) {
        g = guid(j.get<std::string>());
    }


    DATASUITE_API guid new_guid();
}

#endif