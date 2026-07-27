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
        uint32_t Id;
        OrderType Type;
        OrderSide Side;
        std::int64_t Price;
        std::int32_t ClientId;
        std::uint32_t Amount;
        std::int32_t Asset;
        std::int64_t Timestamp;

    public:
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

        void log() noexcept;
    };
#pragma pack(pop)

    bool parseBinOrder(const char *binstr, size_t n, Order *output) noexcept;
    void serializeOrder(const Order &order,
                        char buffer[sizeof(Order)]) noexcept;
} // namespace MarketExecution
