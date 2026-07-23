#include "datasuite/guid.hpp"
#include <array>
#include <cstddef>
#include <format> // Replaces sstream, iomanip, and hex_string

#ifdef GUID_LIBUUID
#include <uuid/uuid.h>
#endif

#ifdef GUID_CFUUID
#include <CoreFoundation/CFUUID.h>
#endif

#ifdef GUID_WINDOWS
#include <objbase.h>
#endif

namespace datasuite
{
    Guid newGuid()
    {
        static constexpr std::size_t GUID_SIZE = 16;
        std::array<unsigned char, GUID_SIZE> raw_bytes;

#ifdef GUID_LIBUUID
        uuid_t id;
        uuid_generate(id);
        std::copy(id, id + GUID_SIZE, raw_bytes.begin());
#endif

#ifdef GUID_CFUUID
        auto id = CFUUIDCreate(NULL);
        auto bytes = CFUUIDGetUUIDBytes(id);
        CFRelease(id);

        raw_bytes =
        {
            bytes.byte0,  bytes.byte1,  bytes.byte2,  bytes.byte3,
            bytes.byte4,  bytes.byte5,  bytes.byte6,  bytes.byte7,
            bytes.byte8,  bytes.byte9,  bytes.byte10, bytes.byte11,
            bytes.byte12, bytes.byte13, bytes.byte14, bytes.byte15
        };
#endif

#ifdef GUID_WINDOWS
        GUID id;
        CoCreateGuid(&id);

        using uchar = unsigned char;

        // Windows GUID struct requires byte-shifting to match standard network byte order
        raw_bytes =
        {
            uchar(id.Data1 >> 24 & 0xFF), uchar(id.Data1 >> 16 & 0xFF), 
            uchar(id.Data1 >> 8 & 0xFF),  uchar(id.Data1 & 0xFF),
            uchar(id.Data2 >> 8 & 0xFF),  uchar(id.Data2 & 0xFF),
            uchar(id.Data3 >> 8 & 0xFF),  uchar(id.Data3 & 0xFF),
            id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3],
            id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]
        };
#endif

        // --- THE MODERN STRING BIT ---
        
        // Create our zero-initialized, 64-byte aligned stack array
        Guid buffer{};

        // Write the 16 bytes directly into the stack array formatted as standard UUID hex.
        // {:02x} ensures each byte is printed as exactly 2 lowercase hex characters.
        std::format_to(
            buffer.data(), 
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            raw_bytes[0], raw_bytes[1], raw_bytes[2], raw_bytes[3],
            raw_bytes[4], raw_bytes[5],
            raw_bytes[6], raw_bytes[7],
            raw_bytes[8], raw_bytes[9],
            raw_bytes[10], raw_bytes[11], raw_bytes[12], raw_bytes[13], raw_bytes[14], raw_bytes[15]
        );

        return buffer;
    }
}
