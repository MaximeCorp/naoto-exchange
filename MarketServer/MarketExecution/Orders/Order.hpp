#pragma once

#include <chrono>
#include <iostream>
namespace MarketExecution
{
    using PreciseTimestamp = std::chrono::time_point<std::chrono::steady_clock>;

    enum class OrderType
    {
        LIMIT,
        MARKET
    };
    enum class OrderSide
    {
        BUY,
        SELL
    };

    class Order
    {
    private:
        OrderType Type;
        OrderSide Side;
        float Price;
        int ClientId;
        int Amount;
        int Asset;
        PreciseTimestamp Timestamp;

    public:
        Order(OrderType type, OrderSide side, float price, int client_id,
              int amount, int asset);
        Order(OrderType type, OrderSide side, int client_id, int amount,
              int asset);
        ~Order() = default;

        const OrderType &getType();
        const OrderSide &getSide();
        const float &getPrice();
        const int &getClientId();
        const int &getAmount();
        const int &getAsset();
        const PreciseTimestamp &getTimestamp();

        void setAmount(int amout);

        void log();
    };
} // namespace MarketExecution
