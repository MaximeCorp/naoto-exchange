#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace naoto
{
#pragma pack(push, 1)
    enum class OrderType : uint8_t
    {
        LIMIT = 0,
        MARKET = 1,
    };
    enum class OrderSide : uint8_t
    {
        BUY = 0,
        SELL = 1,
    };

    enum class OrderAction : uint8_t
    {
        EXECUTE = 0,
        CANCEL = 1,
    };

    struct Order
    {
        int64_t Price;
#ifdef NAOTO_PERF
        uint64_t IngestedTimestamp;
        uint64_t RoutedTimestamp;
        uint64_t ReceivedTimestamp;
#endif
        uint64_t OrderId;
        uint32_t ClientOrderId;
        uint32_t ClientId;
        uint32_t Amount;
        uint16_t AssetId;
        OrderType Type;
        OrderSide Side;
        OrderAction Action;
        std::array<uint8_t, 7> Padding;

        void log() const noexcept;
    };
#pragma pack(pop)
} // namespace naoto
