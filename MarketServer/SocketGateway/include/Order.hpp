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

    constexpr size_t MAX_KEY_LEN = 10;

    struct Order
    {
    private:
        char Key[MAX_KEY_LEN];
        OrderType Type;
        OrderSide Side;
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

        const char *getKey() const;
        const OrderType &getType() const;
        const OrderSide &getSide() const;
        const float &getPrice() const;
        const std::int32_t &getClientId() const;
        const float &getAmount() const;
        const std::uint16_t &getAsset() const;
        const std::int64_t &getTimestamp() const;

        void setAmount(float amout);
        void setType(OrderType type);
        void setSide(OrderSide side);
        void setPrice(float price);
        void setClientId(std::int32_t clientId);
        void setAsset(std::uint16_t asset);
        void setTimestamp(std::int64_t timestamp);

        void log() const;
    };
#pragma pack(pop)

    bool parseBinOrder(const char *binstr, size_t n, Order *output);
    void serializeOrder(const Order &order, char buffer[sizeof(Order)]);
} // namespace Gateways
