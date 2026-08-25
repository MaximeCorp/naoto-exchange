#pragma once

#include <cstdint>

namespace MarketExecution
{
#pragma pack(push, 1)
    enum class OrderState : uint32_t
    {
        FILL = 0,
        PARTIAL_FILL = 1,
        CANCEL = 2,
        REJECT = 3,
        ADD = 4,
    };

    struct OrderStateReport
    {
        int64_t BoughtDelta;
        int64_t SoldDelta;
        int64_t SoldAttemptDelta;
        uint32_t SequenceId;
        uint32_t ClientId;
        uint32_t OrderId;
        uint32_t TradeId;
        uint16_t BoughtAssetId;
        uint16_t SoldAssetId;
        OrderState State;

        void FillReport(int64_t boughtDelta, int64_t soldDelta,
                        int64_t soldAttemptDelta, uint32_t sequenceId,
                        uint32_t clientId, uint32_t orderId, uint32_t tradeId,
                        uint16_t boughtAssetId, uint16_t soldAssetId,
                        OrderState state)
        {
            BoughtDelta = boughtDelta;
            SoldDelta = soldDelta;
            SoldAttemptDelta = soldAttemptDelta;
            SequenceId = sequenceId;
            ClientId = clientId;
            OrderId = orderId;
            TradeId = tradeId;
            BoughtAssetId = boughtAssetId;
            SoldAssetId = soldAssetId;
            State = state;
        }
    };
#pragma pack(pop)
} // namespace MarketExecution
