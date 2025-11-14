#pragma once

#include <Asset.hpp>
#include <Order.hpp>
#include <boost/lockfree/queue.hpp>
#include <librdkafka/rdkafkacpp.h>
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

        std::vector<Order> *GetBestOffers(Order &order);

        bool IsMarketable(Order &order);

        void FillOffer(Order &order, std::vector<Order> *bestOffers);

        void ExecuteOrder(Order &order);

        void ExecuteMarketableOrder(Order &order);

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

        void
        consumeOrdersQueueLoop(boost::lockfree::queue<Order *> &OrdersQueue,
                               boost::lockfree::queue<Order *> &StatusQueue,
                               std::atomic<bool> &running);

        void AddOrder(Order &order);

        void AddLimitOrder(std::string key, OrderSide side, float price,
                           std::int32_t clientId, float amount,
                           std::int64_t timestamp);
        void AddMarketOrder(std::string key, OrderSide side,
                            std::int32_t clientId, float amount,
                            std::int64_t timestamp);

        float getMarketPrice();

        int getMarketAssetId();
    };

    void activateOrdersLoop(BidAsk &matchingEngine,
                            boost::lockfree::queue<Order *> &OrdersQueue,
                            boost::lockfree::queue<Order *> &StatusQueue,
                            std::atomic<bool> &running);
} // namespace MarketExecution
