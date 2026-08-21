#include <Order.hpp>
#include <arpa/inet.h>
#include <cstring>

[[nodiscard]] uint64_t htonll(uint64_t hostval) noexcept
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap64(hostval);
#else
    return hostval;
#endif
};

[[nodiscard]] uint64_t ntohll(uint64_t netval) noexcept
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
        : Id()
        , Type(OrderType::LIMIT)
        , Side(OrderSide::BUY)
        , Price(0)
        , ClientId(0)
        , Amount(0)
        , Asset(0)
        , Timestamp(0)
    {}
    Order::Order(const uint32_t id, OrderType type, OrderSide side,
                 std::int64_t price, std::int32_t client_id,
                 std::uint32_t amount, std::int32_t asset,
                 std::int64_t timestamp)
        : Id(id)
        , Type(type)
        , Side(side)
        , Price(price)
        , ClientId(client_id)
        , Amount(amount)
        , Asset(asset)
        , Timestamp(timestamp)
    {}

    Order::Order(const uint32_t id, OrderType type, OrderSide side,
                 std::int32_t client_id, std::uint32_t amount,
                 std::int32_t asset, std::int64_t timestamp)
        : Id(id)
        , Type(type)
        , Side(side)
        , ClientId(client_id)
        , Amount(amount)
        , Asset(asset)
        , Timestamp(timestamp)
    {
        Price = -1;
    }

    void Order::log() const noexcept
    {
        std::string side = Side == OrderSide::BUY ? "BUY" : "SELL";
        std::string type = Type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        std::cout << side << " " << type << " ORDER - Amount: " << Amount
                  << " - Price: " << Price << " - Client ID: " << ClientId
                  << " id:" << Id << "\n"
                  << std::endl;
    }

    [[nodiscard]] const uint32_t &Order::getId() const noexcept
    {
        return Id;
    }
    [[nodiscard]] const OrderType &Order::getType() const noexcept
    {
        return Type;
    }
    [[nodiscard]] const OrderSide &Order::getSide() const noexcept
    {
        return Side;
    }
    [[nodiscard]] const std::int64_t &Order::getPrice() const noexcept
    {
        return Price;
    }
    [[nodiscard]] const std::int32_t &Order::getClientId() const noexcept
    {
        return ClientId;
    }
    [[nodiscard]] const std::uint32_t &Order::getAmount() const noexcept
    {
        return Amount;
    }
    [[nodiscard]] const std::int32_t &Order::getAsset() const noexcept
    {
        return Asset;
    }
    [[nodiscard]] const std::int64_t &Order::getTimestamp() const noexcept
    {
        return Timestamp;
    }
    void Order::setId(const uint32_t id) noexcept
    {
        Id = id;
    }
    void Order::setAmount(std::uint32_t amout) noexcept
    {
        Amount = amout;
    }
    void Order::setType(OrderType type) noexcept
    {
        Type = type;
    }
    void Order::setSide(OrderSide side) noexcept
    {
        Side = side;
    }
    void Order::setPrice(std::int64_t price) noexcept
    {
        Price = price;
    }
    void Order::setClientId(std::int32_t clientId) noexcept
    {
        ClientId = clientId;
    }
    void Order::setAsset(std::int32_t asset) noexcept
    {
        Asset = asset;
    }
    void Order::setTimestamp(std::int64_t timestamp) noexcept
    {
        Timestamp = timestamp;
    }

    [[nodiscard]] bool parseBinOrder(const char *binstr, size_t n,
                                     Order *output) noexcept
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

        std::memcpy((void *)&output->getId(), ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        std::int32_t type_net;
        std::memcpy(&type_net, ptr, sizeof(type_net));
        output->setType(static_cast<OrderType>(ntohl(type_net)));
        ptr += sizeof(type_net);

        std::int32_t side_net;
        std::memcpy(&side_net, ptr, sizeof(side_net));
        output->setSide(static_cast<OrderSide>(ntohl(side_net)));
        ptr += sizeof(side_net);

        std::int64_t price_bytes_net;
        std::memcpy(&price_bytes_net, ptr, sizeof(price_bytes_net));
        price_bytes_net = ntohl(price_bytes_net);
        std::int64_t price;
        std::memcpy(&price, &price_bytes_net, sizeof(price));
        output->setPrice(price);
        ptr += sizeof(price_bytes_net);

        std::int32_t client_id_net;
        std::memcpy(&client_id_net, ptr, sizeof(client_id_net));
        output->setClientId(ntohl(client_id_net));
        ptr += sizeof(client_id_net);

        std::uint32_t amount_bytes_net;
        std::memcpy(&amount_bytes_net, ptr, sizeof(amount_bytes_net));
        amount_bytes_net = ntohl(amount_bytes_net);
        std::uint32_t amount;
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

    void serializeOrder(const Order &order, char buffer[sizeof(Order)]) noexcept
    {
        char *ptr = buffer;

        std::memcpy(ptr, &order.getId(), sizeof(uint32_t));
        ptr += sizeof(uint32_t);

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
