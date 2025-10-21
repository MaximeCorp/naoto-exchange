#include "BidAsk.hpp"

#include <iostream>

namespace MarketExecution
{
    BidAsk::BidAsk(Asset asset, float initialPrice)
        : MarketAsset(asset)
        , MarketPrice(initialPrice)
    {
        Bid = std::map<float, std::vector<Order>>();
        Ask = std::map<float, std::vector<Order>, std::greater<>>();
        MarketOrders = std::vector<Order>();
    }

    Asset BidAsk::getMarketAsset()
    {
        std::lock_guard<std::mutex> lock(MarketAssetMutex);

        return MarketAsset;
    }

    int BidAsk::getMarketAssetId()
    {
        std::lock_guard<std::mutex> lock(MarketAssetMutex);

        return MarketAsset.getId();
    }

    float BidAsk::getMarketPrice()
    {
        std::lock_guard<std::mutex> lock(MarketPriceMutex);

        return MarketPrice;
    }

    const std::map<float, std::vector<Order>> BidAsk::getBid()
    {
        std::lock_guard<std::mutex> lock(BidMutex);

        return Bid;
    }

    const std::map<float, std::vector<Order>, std::greater<>> BidAsk::getAsk()
    {
        std::lock_guard<std::mutex> lock(AskMutex);

        return Ask;
    }

    const std::vector<Order> BidAsk::getMarketOrders()
    {
        std::lock_guard<std::mutex> lock(MarketOrdersMutex);

        return MarketOrders;
    }

    int BidAsk::getMarketOrdersSize()
    {
        std::lock_guard<std::mutex> lock(MarketOrdersMutex);
        return MarketOrders.size();
    }

    bool BidAsk::getFirstOrder(Order *order)
    {
        std::lock_guard<std::mutex> lock(MarketOrdersMutex);

        if (MarketOrders.empty())
        {
            return false;
        }

        *order = MarketOrders.front();
        MarketOrders.erase(MarketOrders.begin());

        return true;
    }

    void BidAsk::AddOrder(Order order)
    {
        std::mutex &toLock = IsMarketable(order) ? MarketOrdersMutex
            : order.getSide() == OrderSide::BUY  ? AskMutex
                                                 : BidMutex;
        std::lock_guard<std::mutex> lock(toLock);

        std::thread t(&BidAsk::AddOrderInsider, this, order, true);
        t.detach();
    }

    void BidAsk::AddLimitOrder(OrderSide side, float price, int clientId,
                               int amount)
    {
        Order order = Order(OrderType::LIMIT, side, price, clientId, amount,
                            MarketAsset.getId());
        AddOrder(order);
    }

    void BidAsk::AddMarketOrder(OrderSide side, int clientId, int amount)
    {
        Order order = Order(OrderType::MARKET, side, clientId, amount,
                            MarketAsset.getId());
        AddOrder(order);
    }

    // No mutex lock: should only be used after locking
    void BidAsk::AddOrderInsider(Order order, bool debug)
    {
        if (!IsMarketable(order))
        {
            if (order.getSide() == OrderSide::BUY)
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
            if (debug)
            {
                std::cout << "(MARKETABLE)\n\n";
            }

            MarketOrders.emplace_back(order);
        }
        if (debug)
        {
            std::cout << "New order:\n";
            order.log();
        }
    }

    // No mutex lock: should only be used after locking
    std::vector<Order> *BidAsk::GetBestOffers(Order order)
    {
        if (order.getSide() == OrderSide::BUY)
        {
            // Lowest price above market price
            auto it = Bid.lower_bound(MarketPrice);

            while (it != Bid.end() && it->second.size() == 0)
            {
                Bid.erase(it);
                it = Bid.lower_bound(MarketPrice);
            }

            if (it != Bid.end())
            {
                return &(it->second);
            }
        }
        else
        {
            // Highest price bellow market price
            auto it = Ask.lower_bound(MarketPrice);

            while (it != Ask.end() && it->second.size() == 0)
            {
                Bid.erase(it);
                it = Ask.lower_bound(MarketPrice);
            }

            if (it != Ask.end())
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
            if (order.getPrice() > getMarketPrice())
            {
                return true;
            }
        }
        else
        {
            if (order.getPrice() < getMarketPrice())
            {
                return true;
            }
        }

        return false;
    }

    // No mutex lock: should only be used after locking
    void BidAsk::FillOffer(Order order, std::vector<Order> *bestOffers)
    {
        // Get first offer
        Order &curOrder = bestOffers->front();
        MarketPrice = curOrder.getPrice();

        std::cout << "Consuming limit order:\n";
        curOrder.log();

        // Execute it until order or offer is filled
        while (curOrder.getAmount() > 0 && order.getAmount() > 0)
        {
            float ratio =
                (float)curOrder.getAmount() / (float)order.getAmount();
            if (ratio == 1)
            {
                // Orders fill each other
                bestOffers->erase(bestOffers->begin());
                order.setAmount(0);
                std::cout << "Both filled";
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
        }

        std::cout << "MARKET PRICE: " << MarketPrice << "\n";
    }

    void BidAsk::ExecuteMarketableOrder(Order order)
    {
        // Lock the right side of the order book
        std::mutex &toLock =
            order.getSide() == OrderSide::BUY ? BidMutex : AskMutex;
        std::lock_guard<std::mutex> lock(toLock);
        std::vector<Order> *bestOffers = GetBestOffers(order);

        while (bestOffers && bestOffers->size() > 0 && order.getAmount() > 0)
        {
            // Fill one offer
            FillOffer(order, bestOffers);
            // Update best offers according to new price
            bestOffers = GetBestOffers(order);
        }

        if (order.getAmount() > 0)
        {
            if (IsMarketable(order))
            {
                std::lock_guard<std::mutex> lock(MarketOrdersMutex);
                AddOrderInsider(order, false);
            }
            else
            {
                AddOrderInsider(order, false);
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
                std::lock_guard<std::mutex> lock(AskMutex);
                Ask[order.getPrice()].emplace_back(order);
            }
            else
            {
                std::lock_guard<std::mutex> lock(BidMutex);
                Bid[order.getPrice()].emplace_back(order);
            }

            return;
        }

        // Execute if marketable
        ExecuteMarketableOrder(order);
    }

    void BidAsk::MarketExecutionLoop()
    {
        while (true)
        {
            Order firstOrder;

            // Do nothing if no market orders
            if (!getFirstOrder(&firstOrder))
            {
                continue;
            }

            // Execute it
            std::thread t(&BidAsk::ExecuteOrder, this, firstOrder);
            t.detach();
        }
    }

    std::thread BidAsk::StartMarketExecution()
    {
        std::thread t(&BidAsk::MarketExecutionLoop, this);

        return t;
    }
} // namespace MarketExecution
