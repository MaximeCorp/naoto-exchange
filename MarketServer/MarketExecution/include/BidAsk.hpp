#pragma once

#include <Asset.hpp>
#include <Order.hpp>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace MarketExecution
{
    class BidAsk
    {
    private:
        Asset MarketAsset;
        std::mutex MarketAssetMutex;
        float MarketPrice;
        std::mutex MarketPriceMutex;

        std::map<float, std::vector<Order>> Bid;
        std::mutex BidMutex;
        std::map<float, std::vector<Order>, std::greater<>> Ask;
        std::mutex AskMutex;
        std::vector<Order> MarketOrders;
        std::mutex MarketOrdersMutex;

        std::vector<Order> *GetBestOffers(Order order);

        bool IsMarketable(Order order);

        void FillOffer(Order order, std::vector<Order> *bestOffers);

        void ExecuteOrder(Order order);

        void ExecuteMarketableOrder(Order order);

        void MarketExecutionLoop();

        void AddOrderInsider(Order order, bool debug = true);

        Asset getMarketAsset();

        int getMarketOrdersSize();

        bool getFirstOrder(Order *order);

        const std::map<float, std::vector<Order>> getBid();

        const std::map<float, std::vector<Order>, std::greater<>> getAsk();

        const std::vector<Order> getMarketOrders();

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
