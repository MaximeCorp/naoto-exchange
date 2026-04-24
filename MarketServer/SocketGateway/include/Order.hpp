#pragma once

#include <chrono>
#include <iostream>

namespace Gateways
{
#pragma pack(push, 1)
    enum class OrderType : std::int32_t
    {
        LIMIT,
        MARKET
    };
    enum class OrderSide : std::int32_t
    {
        BUY,
        SELL
    };
    enum class OrderStatus : std::int32_t
    {
        ACCEPTED,
        REJECTED,
        EXECUTED,
        CANCELLED
    };

    constexpr size_t MAX_KEY_LEN = 10;

    struct Order
    {
    private:
        char Key[MAX_KEY_LEN];
        OrderType Type;
        OrderSide Side;
        OrderStatus Status;
        float Price;
        std::int32_t ClientId;
        float Amount;
        std::uint16_t Asset;
        std::int64_t Timestamp;

    public:
        Order();
        Order(const char *key, OrderType type, OrderSide side, float price,
              std::int32_t client_id, float amount, std::int32_t asset,
              std::int64_t timestamp);
        Order(const char *key, OrderType type, OrderSide side,
              std::int32_t client_id, float amount, std::int32_t asset,
              std::int64_t timestamp);
        ~Order() = default;

        [[nodiscard]] char *getKey() const;
        [[nodiscard]] OrderType getType() const;
        [[nodiscard]] OrderSide getSide() const;
        [[nodiscard]] OrderSide getSide() const;
        [[nodiscard]] float getPrice() const;
        [[nodiscard]] std::int32_t getClientId() const;
        [[nodiscard]] float getAmount() const;
        [[nodiscard]] std::uint16_t getAsset() const;
        [[nodiscard]] std::int64_t getTimestamp() const;

        void setAmount(const float amout);
        void setType(const OrderType type);
        void setSide(const OrderSide side);
        void setStatus(const OrderStatus status);
        void setPrice(const float price);
        void setClientId(const std::int32_t clientId);
        void setAsset(const std::uint16_t asset);
        void setTimestamp(const std::int64_t timestamp);

        void log() const;
    };
#pragma pack(pop)

    bool parseBinOrder(const char *binstr, size_t n, Order *output);
    void serializeOrder(const Order &order, char buffer[sizeof(Order)]);
} // namespace Gateways
