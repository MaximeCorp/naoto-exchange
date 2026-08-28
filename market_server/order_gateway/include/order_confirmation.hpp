#pragma once

#include <cstdint>

namespace naoto::order_gateway
{
    enum class OrderConfirmationStatus : uint8_t
    {
        Accepted = 0,
        InsufficientFunds = 1,
        MaxPositions = 2,
        InvalidPrice = 3,
        InvalidQuantity = 4,
        UnknownSymbol = 5,
        TechnicalFailure = 6,
        UserNotConnected = 7,
        BadClientId = 8,
    };
#pragma pack(push, 1)
    struct OrderConfirmation
    {
        uint32_t OrderId;
        uint32_t ClientOrderId;
        OrderConfirmationStatus Status;
    };
#pragma pack(pop)
} // namespace naoto::order_gateway