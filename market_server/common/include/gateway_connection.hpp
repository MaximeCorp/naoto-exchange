#pragma once

#include <cstdint>

namespace naoto
{
#pragma pack(push, 1)
    struct GatewayConnection
    {
        uint16_t GatewayId;
    };
#pragma pack(pop)
} // namespace naoto