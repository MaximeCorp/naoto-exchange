#pragma once

#include <cstdint>

namespace naoto
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
#ifdef NAOTO_PERF
        uint64_t IngestedTimestamp;
        uint64_t ReceivedTimestamp;
        uint64_t UpdateTimestamp;
#endif
        uint32_t SequenceId;
        uint32_t ClientId;
        uint32_t OrderId;
        uint32_t TradeId;
        uint16_t BoughtAssetId;
        uint16_t SoldAssetId;
        OrderState State;

        void FillReport(int64_t boughtDelta, int64_t soldDelta,
                        int64_t soldAttemptDelta,
#ifdef NAOTO_PERF
                        uint64_t ingestedTimestamp, uint64_t receivedTimestamp,
                        uint64_t updateTimestamp,
#endif
                        uint32_t sequenceId, uint32_t clientId,
                        uint32_t orderId, uint32_t tradeId,
                        uint16_t boughtAssetId, uint16_t soldAssetId,
                        OrderState state)
        {
            BoughtDelta = boughtDelta;
            SoldDelta = soldDelta;
            SoldAttemptDelta = soldAttemptDelta;
#ifdef NAOTO_PERF
            IngestedTimestamp = ingestedTimestamp;
            ReceivedTimestamp = receivedTimestamp;
            UpdateTimestamp = updateTimestamp;
#endif
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
} // namespace naoto
