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

    const size_t MAX_KEY_LEN = 25;

    struct Order
    {
    private:
        char Key[MAX_KEY_LEN];
        OrderType Type;
        OrderSide Side;
        std::int64_t Price;
        std::int32_t ClientId;
        std::uint32_t Amount;
        std::int32_t Asset;
        std::int64_t Timestamp;

    public:
        Order();
        Order(const char *key, OrderType type, OrderSide side,
              std::int64_t price, std::int32_t client_id, std::uint32_t amount,
              std::int32_t asset, std::int64_t timestamp);
        Order(const char *key, OrderType type, OrderSide side,
              std::int32_t client_id, std::uint32_t amount, std::int32_t asset,
              std::int64_t timestamp);
        ~Order() = default;

        const char *getKey() const;
        const OrderType &getType() const;
        const OrderSide &getSide() const;
        const std::int64_t &getPrice() const;
        const std::int32_t &getClientId() const;
        const std::uint32_t &getAmount() const;
        const int &getAsset() const;
        const std::int64_t &getTimestamp() const;

        void setAmount(std::uint32_t amout);
        void setType(OrderType type);
        void setSide(OrderSide side);
        void setPrice(std::int64_t price);
        void setClientId(std::int32_t clientId);
        void setAsset(std::int32_t asset);
        void setTimestamp(std::int64_t timestamp);

        void log() const;
    };
#pragma pack(pop)

    bool parseBinOrder(const char *binstr, size_t n, Order *output);
    void serializeOrder(const Order &order, char buffer[sizeof(Order)]);
} // namespace Gateways
