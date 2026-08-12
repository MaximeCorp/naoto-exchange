#pragma once

#include <array>
#include <cstdint>

namespace Gateways
{
#pragma pack(push, 1)
    struct ClientRequest
    {
        char RequestType; // 'A' for client details request
                          // 'D' For client disconnect
        uint32_t ClientId;
        std::array<uint8_t, 32> Key; // Used for client details request only
    };

#pragma pack(pop)
} // namespace Gateways
