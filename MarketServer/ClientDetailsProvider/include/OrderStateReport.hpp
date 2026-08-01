#pragma once

#include <cstdint>

namespace AccountService
{
#pragma pack(push, 1)
    struct OrderStateReport
    {
        uint32_t SequenceId;
        int64_t BoughtDelta; // Applied negatively on the sold asset
        int64_t SoldDelta;
        uint32_t ClientId;
        uint32_t OrderId;
        uint32_t TradeId;
        uint16_t BoughtAssetId;
        uint16_t SoldAssetId;

        void FillReport(int64_t boughtDelta, int64_t soldDelta,
                        uint32_t clientId, uint32_t orderId, uint32_t tradeId,
                        uint16_t boughtAssetId, uint16_t soldAssetId)
        {
            BoughtDelta = boughtDelta;
            SoldDelta = soldDelta;
            ClientId = clientId;
            OrderId = orderId;
            TradeId = tradeId;
            BoughtAssetId = boughtAssetId;
            SoldAssetId = soldAssetId;
        }
    };
#pragma pack(pop)
} // namespace AccountService
