#include <Order.hpp>

namespace MarketExecution
{
    Order::Order()
    {}
    Order::Order(OrderType type, OrderSide side, float price, int client_id,
                 int amount, int asset)
        : Type(type)
        , Side(side)
        , Price(price)
        , ClientId(client_id)
        , Amount(amount)
        , Asset(asset)
        , Timestamp(std::chrono::time_point<std::chrono::steady_clock>())
    {}

    Order::Order(OrderType type, OrderSide side, int client_id, int amount,
                 int asset)
        : Type(type)
        , Side(side)
        , ClientId(client_id)
        , Amount(amount)
        , Asset(asset)
        , Timestamp(std::chrono::time_point<std::chrono::steady_clock>())
    {
        Price = -1;
    }

    void Order::log()
    {
        std::string side = Side == OrderSide::BUY ? "BUY" : "SELL";
        std::string type = Type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        std::cout << side << " " << type << " ORDER - Amount: " << Amount
                  << " - Price: " << Price << " - Client ID: " << ClientId
                  << "\n\n";
    }

    const OrderType &Order::getType()
    {
        return Type;
    }
    const OrderSide &Order::getSide()
    {
        return Side;
    };
    const float &Order::getPrice()
    {
        return Price;
    }
    const int &Order::getClientId()
    {
        return ClientId;
    };
    const int &Order::getAmount()
    {
        return Amount;
    };
    const int &Order::getAsset()
    {
        return Asset;
    };
    const PreciseTimestamp &Order::getTimestamp()
    {
        return Timestamp;
    };
    void Order::setAmount(int amout)
    {
        Amount = amout;
    }
} // namespace MarketExecution
