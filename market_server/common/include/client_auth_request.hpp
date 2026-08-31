#pragma once

#include <array>
#include <cstdint>

namespace naoto
{
#pragma pack(push, 1)
    struct ClientAuthRequest
    {
        char RequestType; // 'A' for client details request
                          // 'D' For client disconnect
        uint32_t ClientId;
        std::array<uint8_t, 32> Key; // Used for client details request only
    };

#pragma pack(pop)
} // namespace naoto
