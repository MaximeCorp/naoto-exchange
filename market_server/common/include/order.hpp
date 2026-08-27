#pragma once

#include <array>
#include <chrono>
#include <iostream>

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
        uint64_t Timestamp;
        uint64_t OrderId;
        uint32_t ClientOrderId;
        uint32_t ClientId;
        uint32_t Amount;
        uint16_t AssetId;
        OrderType Type;
        OrderSide Side;
        OrderAction Action;
        std::array<uint8_t, 7> Padding;

        void log() const noexcept
        {
            std::string side = Side == OrderSide::BUY ? "BUY" : "SELL";
            std::string type = Type == OrderType::LIMIT ? "LIMIT" : "MARKET";
            std::cout << side << " " << type << " ORDER - Amount: " << Amount
                      << " - Price: " << Price << " - Client ID: " << ClientId
                      << " order id:" << OrderId << " asset id: " << AssetId
                      << "\n"
                      << std::endl;
        }
    };
#pragma pack(pop)
} // namespace naoto
