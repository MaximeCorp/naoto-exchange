#include <BidAsk.hpp>
#include <iostream>

namespace MarketExecution
{
    BidAsk::BidAsk(Asset asset, float initialPrice)
        : MarketAsset(asset)
        , MarketPrice(initialPrice)
    {
        Bid = std::map<float, std::vector<Order>, std::greater<>>();
        Ask = std::map<float, std::vector<Order>>();
    }

    Asset BidAsk::getMarketAsset()
    {
        return MarketAsset;
    }

    int BidAsk::getMarketAssetId()
    {
        return MarketAsset.getId();
    }

    float BidAsk::getMarketPrice()
    {
        return MarketPrice;
    }

    const std::map<float, std::vector<Order>, std::greater<>> BidAsk::getBid()
    {
        return Bid;
    }

    const std::map<float, std::vector<Order>> BidAsk::getAsk()
    {
        return Ask;
    }

    void BidAsk::AddOrder(Order order)
    {
        if (!IsMarketable(order))
        {
            if (order.getSide() == OrderSide::SELL)
            {
                Ask[order.getPrice()].emplace_back(order);
            }
            else
            {
                Bid[order.getPrice()].emplace_back(order);
            }
        }
        else
        {
            ExecuteMarketableOrder(order);
        }
    }

    void BidAsk::AddLimitOrder(std::string key, OrderSide side, float price,
                               std::int32_t clientId, float amount,
                               std::int64_t timestamp)
    {
        Order order = Order(key.c_str(), OrderType::LIMIT, side, price,
                            clientId, amount, MarketAsset.getId(), timestamp);
        AddOrder(order);
    }

    void BidAsk::AddMarketOrder(std::string key, OrderSide side,
                                std::int32_t clientId, float amount,
                                std::int64_t timestamp)
    {
        Order order = Order(key.c_str(), OrderType::MARKET, side, clientId,
                            amount, MarketAsset.getId(), timestamp);
        AddOrder(order);
    }

    std::vector<Order> *BidAsk::GetBestOffers(Order order)
    {
        if (order.getSide() == OrderSide::BUY)
        {
            if (Ask.empty())
            {
                return nullptr;
            }

            auto it = Ask.begin();

            while (it != Ask.end() && it->second.empty())
            {
                it = Ask.erase(it);
            }

            if (it != Ask.end())
            {
                return &(it->second);
            }
        }
        else
        {
            if (Bid.empty())
            {
                return nullptr;
            }

            auto it = Bid.begin();

            while (it != Bid.end() && it->second.empty())
            {
                it = Bid.erase(it);
            }

            if (it != Bid.end())
            {
                return &(it->second);
            }
        }

        return nullptr;
    }

    bool BidAsk::IsMarketable(Order order)
    {
        // Market orders are always marketable
        if (order.getType() == OrderType::MARKET)
        {
            return true;
        }

        // Compare with market price
        if (order.getSide() == OrderSide::BUY)
        {
            if (order.getPrice() > MarketPrice)
            {
                return true;
            }
        }
        else
        {
            if (order.getPrice() < MarketPrice)
            {
                return true;
            }
        }

        return false;
    }

    void BidAsk::FillOffer(Order &order, std::vector<Order> *bestOffers)
    {
        // Execute it until order or offer is filled
        while (IsMarketable(order) && !bestOffers->empty()
               && order.getAmount() > 0)
        {
            Order &curOrder = bestOffers->front();
            std::cout << "Consuming limit order:\n";
            curOrder.log();
            float ratio =
                (float)curOrder.getAmount() / (float)order.getAmount();
            if (ratio == 1)
            {
                // Orders fill each other
                bestOffers->erase(bestOffers->begin());
                order.setAmount(0);
                std::cout << "Both filled\n\n";
            }
            else if (ratio > 1)
            {
                // Offer is bigger than market order -> decrement offer
                // amount & order set amount to 0
                curOrder.setAmount(curOrder.getAmount() - order.getAmount());
                order.setAmount(0);
                std::cout << "Market order filled\n"
                          << curOrder.getAmount() << " left (offer)\n\n";
            }
            else
            {
                // Offer is smaller than market order -> decrement
                // amount order & set offer amount to 0
                order.setAmount(order.getAmount() - curOrder.getAmount());
                curOrder.setAmount(0);

                bestOffers->erase(bestOffers->begin());
                std::cout << "Limit order filled, looking for next best offer\n"
                          << order.getAmount() << " left (order)\n\n";
            }

            MarketPrice = curOrder.getPrice();
        }

        std::cout << "MARKET PRICE: " << MarketPrice << "\n";
    }

    void BidAsk::ExecuteMarketableOrder(Order order)
    {
        std::vector<Order> *bestOffers = GetBestOffers(order);

        while (IsMarketable(order) && bestOffers && bestOffers->size() > 0
               && order.getAmount() > 0)
        {
            // Fill one offer
            FillOffer(order, bestOffers);
            // Update best offers according to new price
            bestOffers = GetBestOffers(order);
        }

        if (order.getAmount() > 0 && bestOffers && !bestOffers->empty())
        {
            AddOrder(order);
        }
        else if (order.getAmount() > 0 && order.getType() == OrderType::LIMIT
                 && (!bestOffers || bestOffers->empty()))
        {
            if (order.getSide() == OrderSide::SELL)
            {
                Ask[order.getPrice()].emplace_back(order);
            }
            else
            {
                Bid[order.getPrice()].emplace_back(order);
            }
        }
    }

    void BidAsk::ExecuteOrder(Order order)
    {
        // Handle not marketable orders
        if (!IsMarketable(order))
        {
            // Purposefully not using AddOrderInsider for less checks
            if (order.getSide() == OrderSide::BUY)
            {
                Ask[order.getPrice()].emplace_back(order);
            }
            else
            {
                Bid[order.getPrice()].emplace_back(order);
            }

            return;
        }

        // Execute if marketable
        ExecuteMarketableOrder(order);
    }
} // namespace MarketExecution
