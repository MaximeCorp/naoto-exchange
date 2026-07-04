#pragma once

#include <cstdint>

namespace ClientDetailsProvider
{
#pragma pack(push, 1)
    struct ClientUpdate
    {
        char Type; // 'M' if it affects confirmed field, 'A' if it updates
                   // client authorizations
        uint32_t ClientId;
        int64_t Delta; // Use as absolute value for authorization updates
        uint16_t AssetId;
        // Might need a sequence id to handle cases where an update is sent
        // twice
    };
#pragma pack(pop)
} // namespace ClientDetailsProvider
