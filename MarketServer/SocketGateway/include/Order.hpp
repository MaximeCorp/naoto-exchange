#pragma once

#include <array>
#include <chrono>
#include <iostream>

namespace Gateways
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
        uint32_t OrderId;
        uint32_t ClientSideId;
        uint32_t ClientId;
        uint32_t Amount;
        uint16_t AssetId;
        OrderType Type;
        OrderSide Side;
        OrderAction Action;
        std::array<uint8_t, 3> Padding;

        Order();
        Order(const uint32_t id, OrderType type, OrderSide side,
              std::int64_t price, std::int32_t client_id, std::uint32_t amount,
              std::int32_t asset, std::int64_t timestamp);
        Order(const uint32_t id, OrderType type, OrderSide side,
              std::int32_t client_id, std::uint32_t amount, std::int32_t asset,
              std::int64_t timestamp);
        ~Order() = default;

        const uint32_t &getId() const noexcept;
        const OrderType &getType() const noexcept;
        const OrderSide &getSide() const noexcept;
        const std::int64_t &getPrice() const noexcept;
        const std::int32_t &getClientId() const noexcept;
        const std::uint32_t &getAmount() const noexcept;
        const int &getAsset() const noexcept;
        const std::int64_t &getTimestamp() const noexcept;

        void setId(const uint32_t id) noexcept;
        void setAmount(std::uint32_t amout) noexcept;
        void setType(OrderType type) noexcept;
        void setSide(OrderSide side) noexcept;
        void setPrice(std::int64_t price) noexcept;
        void setClientId(std::int32_t clientId) noexcept;
        void setAsset(std::int32_t asset) noexcept;
        void setTimestamp(std::int64_t timestamp) noexcept;

        void log() const noexcept;
    };
#pragma pack(pop)

    bool parseBinOrder(const char *binstr, size_t n, Order *output) noexcept;
    void serializeOrder(const Order &order,
                        char buffer[sizeof(Order)]) noexcept;
} // namespace Gateways
