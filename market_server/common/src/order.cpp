#include <order.hpp>

#include <iostream>
#include <string>

namespace naoto
{
    void Order::log() const noexcept
    {
        std::string side = Side == OrderSide::BUY ? "BUY" : "SELL";
        std::string type = Type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        std::cout << side << " " << type << " ORDER - Amount: " << Amount
                  << " - Price: " << Price << " - Client ID: " << ClientId
                  << " order id:" << OrderId << " asset id: " << AssetId
                  << "\n"
                  << std::endl;
    }
} // namespace naoto
