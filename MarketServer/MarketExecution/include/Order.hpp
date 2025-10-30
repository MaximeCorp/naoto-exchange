#pragma once

#include <chrono>
#include <iostream>
namespace MarketExecution
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
        float Price;
        std::int32_t ClientId;
        float Amount;
        std::int32_t Asset;
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

        const char *getKey();
        const OrderType &getType();
        const OrderSide &getSide();
        const float &getPrice();
        const std::int32_t &getClientId();
        const float &getAmount();
        const int &getAsset();
        const std::int64_t &getTimestamp();

        void setAmount(float amout);

        void log();
    };
#pragma pack(pop)

    bool parseBinOrder(const char *binstr, size_t n, Order *output);
} // namespace MarketExecution
