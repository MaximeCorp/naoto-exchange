#pragma once

#include <cstdint>

namespace MarketExecution
{
#pragma pack(push, 1)
    struct OrderStateReport
    {
        int64_t Delta; // Applied negatively on the sold asset
        uint32_t SequenceId;
        uint32_t ClientId;
        uint32_t OrderId;
        uint32_t TradeId;
        uint16_t BoughtAssetId;
        uint16_t SoldAssetId;
    };
#pragma pack(pop)
} // namespace MarketExecution
