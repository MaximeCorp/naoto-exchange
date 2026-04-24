#include <Order.hpp>
#include <arpa/inet.h>
#include <cstring>

[[nodiscard]] static inline uint64_t htonll(uint64_t hostval) noexcept
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap64(hostval);
#else
    return hostval;
#endif
};

[[nodiscard]] static inline uint64_t ntohll(uint64_t netval) noexcept
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap64(netval);
#else
    return netval;
#endif
}

namespace Gateways
{
    Order::Order()
        : Key()
        , Type(OrderType::LIMIT)
        , Side(OrderSide::BUY)
        , Price(0)
        , ClientId(0)
        , Amount(0)
        , Asset(0)
        , Timestamp(0)
    {}
    Order::Order(const char *key, const OrderType type, const OrderSide side,
                 const float price, const std::int32_t client_id,
                 const float amount, const std::int32_t asset,
                 const std::int64_t timestamp)
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

    Order::Order(const char *key, const OrderType type, const OrderSide side,
                 const std::int32_t client_id, const float amount,
                 const std::int32_t asset, const std::int64_t timestamp)
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

    void Order::log() const
    {
        std::string side = Side == OrderSide::BUY ? "BUY" : "SELL";
        std::string type = Type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        std::cout << side << " " << type << " ORDER - Amount: " << Amount
                  << " - Price: " << Price << " - Client ID: " << ClientId
                  << "key:";
        std::cout.write(Key, MAX_KEY_LEN);
        std::cout << "\n" << std::endl;
    }

    [[nodiscard]] char *Order::getKey() const
    {
        return Key;
    }
    [[nodiscard]] OrderType Order::getType() const
    {
        return Type;
    }
    [[nodiscard]] OrderSide Order::getSide() const
    {
        return Side;
    }
    [[nodiscard]] float Order::getPrice() const
    {
        return Price;
    }
    [[nodiscard]] std::int32_t Order::getClientId() const
    {
        return ClientId;
    }
    [[nodiscard]] float Order::getAmount() const
    {
        return Amount;
    }
    [[nodiscard]] std::uint16_t Order::getAsset() const
    {
        return Asset;
    }
    [[nodiscard]] std::int64_t Order::getTimestamp() const
    {
        return Timestamp;
    }
    void Order::setAmount(const float amout)
    {
        Amount = amout;
    }
    void Order::setType(const OrderType type)
    {
        Type = type;
    }
    void Order::setSide(const OrderSide side)
    {
        Side = side;
    }
    void Order::setStatus(const OrderStatus status)
    {
        Status = status;
    }
    void Order::setPrice(const float price)
    {
        Price = price;
    }
    void Order::setClientId(const std::int32_t clientId)
    {
        ClientId = clientId;
    }
    void Order::setAsset(const std::uint16_t asset)
    {
        Asset = asset;
    }
    void Order::setTimestamp(const std::int64_t timestamp)
    {
        Timestamp = timestamp;
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

        const char *ptr = binstr;

        std::memcpy((void *)output->getKey(), ptr, MAX_KEY_LEN);
        ptr += MAX_KEY_LEN;

        std::int32_t type_net;
        std::memcpy(&type_net, ptr, sizeof(type_net));
        output->setType(static_cast<OrderType>(ntohl(type_net)));
        ptr += sizeof(type_net);

        std::int32_t side_net;
        std::memcpy(&side_net, ptr, sizeof(side_net));
        output->setSide(static_cast<OrderSide>(ntohl(side_net)));
        ptr += sizeof(side_net);

        std::int32_t price_bytes_net;
        std::memcpy(&price_bytes_net, ptr, sizeof(price_bytes_net));
        price_bytes_net = ntohl(price_bytes_net);
        float price;
        std::memcpy(&price, &price_bytes_net, sizeof(price));
        output->setPrice(price);
        ptr += sizeof(price_bytes_net);

        std::int32_t client_id_net;
        std::memcpy(&client_id_net, ptr, sizeof(client_id_net));
        output->setClientId(ntohl(client_id_net));
        ptr += sizeof(client_id_net);

        std::int32_t amount_bytes_net;
        std::memcpy(&amount_bytes_net, ptr, sizeof(amount_bytes_net));
        amount_bytes_net = ntohl(amount_bytes_net);
        float amount;
        std::memcpy(&amount, &amount_bytes_net, sizeof(amount));
        output->setAmount(amount);
        ptr += sizeof(amount_bytes_net);

        std::int32_t asset_net;
        std::memcpy(&asset_net, ptr, sizeof(asset_net));
        output->setAsset(ntohl(asset_net));
        ptr += sizeof(asset_net);

        uint64_t timestamp_net;
        std::memcpy(&timestamp_net, ptr, sizeof(timestamp_net));
        output->setTimestamp(htonll(timestamp_net));

        return true;
    }

    void serializeOrder(const Order &order, char buffer[sizeof(Order)])
    {
        char *ptr = buffer;

        std::memcpy(ptr, order.getKey(), MAX_KEY_LEN);
        ptr += MAX_KEY_LEN;

        std::int32_t type = htonl(static_cast<std::int32_t>(order.getType()));
        std::memcpy(ptr, &type, sizeof(type));
        ptr += sizeof(type);

        std::int32_t side = htonl(static_cast<std::int32_t>(order.getSide()));
        std::memcpy(ptr, &side, sizeof(side));
        ptr += sizeof(side);

        // IMPORTANT: This only works if float is IEEE 754 standard, which is
        // usually true.
        std::int32_t price_bytes;
        std::memcpy(&price_bytes, &order.getPrice(), sizeof(price_bytes));
        price_bytes = htonl(price_bytes);
        std::memcpy(ptr, &price_bytes, sizeof(price_bytes));
        ptr += sizeof(price_bytes);

        std::int32_t asset = htonl(order.getAsset());
        std::memcpy(ptr, &asset, sizeof(asset));
        ptr += sizeof(asset);

        uint64_t timestamp_net = htonll(order.getTimestamp());
        std::memcpy(ptr, &timestamp_net, sizeof(timestamp_net));
    }
} // namespace Gateways
