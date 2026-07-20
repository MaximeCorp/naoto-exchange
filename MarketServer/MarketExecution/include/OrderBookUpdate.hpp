#pragma once

#include <cstdint>

namespace MarketExecution
{
#pragma pack(push, 1)
    struct OrderBookUpdate
    {
        uint32_t SequenceId;
        uint32_t Depth;
        int64_t Price;
        uint16_t AssetId;
        uint8_t Side;
    };
#pragma pack(pop)
} // namespace MarketExecution
