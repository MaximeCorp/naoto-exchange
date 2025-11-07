#include <Order.hpp>
#include <cstring>

namespace MarketExecution
{
    Order::Order()
    {}
    Order::Order(const char *key, OrderType type, OrderSide side, float price,
                 std::int32_t client_id, float amount, std::int32_t asset,
                 std::int64_t timestamp)
        : Type(type)
        , Side(side)
        , Price(price)
        , ClientId(client_id)
        , Amount(amount)
        , Asset(asset)
        , Timestamp(timestamp)
    {
        size_t key_len = std::min(std::strlen(key), (size_t)MAX_KEY_LEN - 1);
        std::copy(key, key + key_len, Key);
        Key[key_len - 1] = 0;
        std::fill(Key + key_len + 1, Key + MAX_KEY_LEN, 0);
    }

    Order::Order(const char *key, OrderType type, OrderSide side,
                 std::int32_t client_id, float amount, std::int32_t asset,
                 std::int64_t timestamp)
        : Type(type)
        , Side(side)
        , ClientId(client_id)
        , Amount(amount)
        , Asset(asset)
        , Timestamp(timestamp)
    {
        Price = -1;

        size_t key_len = std::min(std::strlen(key), (size_t)MAX_KEY_LEN - 1);
        std::copy(key, key + key_len, Key);
        Key[key_len - 1] = 0;
        std::fill(Key + key_len + 1, Key + MAX_KEY_LEN, 0);
    }

    void Order::log()
    {
        std::string side = Side == OrderSide::BUY ? "BUY" : "SELL";
        std::string type = Type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        std::cout << side << " " << type << " ORDER - Amount: " << Amount
                  << " - Price: " << Price << " - Client ID: " << ClientId
                  << "key:" << Key << "\n"
                  << std::endl;
    }

    const char *Order::getKey()
    {
        return Key;
    }
    const OrderType &Order::getType()
    {
        return Type;
    }
    const OrderSide &Order::getSide()
    {
        return Side;
    }
    const float &Order::getPrice()
    {
        return Price;
    }
    const std::int32_t &Order::getClientId()
    {
        return ClientId;
    }
    const float &Order::getAmount()
    {
        return Amount;
    }
    const std::int32_t &Order::getAsset()
    {
        return Asset;
    }
    const std::int64_t &Order::getTimestamp()
    {
        return Timestamp;
    }
    void Order::setAmount(float amout)
    {
        Amount = amout;
    }

    bool parseBinOrder(const char *binstr, size_t n, Order *output)
    {
        if (!binstr || !output)
        {
            return false;
        }

        if (n != sizeof(Order))
        {
            return false;
        }

        std::memcpy(output, binstr, sizeof(Order));

        return true;
    }
} // namespace MarketExecution
