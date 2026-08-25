#pragma once

#include <cstdint>

namespace Gateways
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
    };
#pragma pack(pop)
} // namespace Gateways
