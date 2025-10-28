#pragma once

#include <Asset.hpp>
#include <Order.hpp>
#include <map>
#include <thread>
#include <vector>

namespace MarketExecution
{
    class BidAsk
    {
    private:
        Asset MarketAsset;
        float MarketPrice;

        std::map<float, std::vector<Order>, std::greater<>> Bid;
        std::map<float, std::vector<Order>> Ask;

        std::vector<Order> *GetBestOffers(Order order);

        bool IsMarketable(Order order);

        void FillOffer(Order &order, std::vector<Order> *bestOffers);

        void ExecuteOrder(Order order);

        void ExecuteMarketableOrder(Order order);

        void MarketExecutionLoop();

        Asset getMarketAsset();

        int getMarketOrdersSize();

        bool getFirstOrder(Order *order);

        const std::map<float, std::vector<Order>, std::greater<>> getBid();

        const std::map<float, std::vector<Order>> getAsk();

    public:
        BidAsk(Asset asset, float initialPrice);
        ~BidAsk() = default;

        // Allow move operations
        BidAsk(BidAsk &&) = default;
        BidAsk &operator=(BidAsk &&) = default;

        std::thread StartMarketExecution();

        void AddOrder(Order order);

        void AddLimitOrder(OrderSide side, float price, int clientId,
                           int amount);
        void AddMarketOrder(OrderSide side, int clientId, int amount);

        float getMarketPrice();

        int getMarketAssetId();
    };
} // namespace MarketExecution
