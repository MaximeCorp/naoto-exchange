#pragma once

#include <cstdint>

namespace AccountService
{
#pragma pack(push, 1)
    struct GatewayConnection
    {
        uint16_t GatewayId;
    };
#pragma pack(pop)
} // namespace AccountService