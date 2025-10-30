#include <BidAsk.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <rdkafkacpp.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace MarketExecution
{
    class TopicListener
    {
    private:
        std::string Topic;
        BidAsk AssetMarket;

        std::mutex AssetMarketMutex;

        bool parseOrder(void *command, size_t n, Order *output);

    public:
        TopicListener(std::string topic, int totalSupply, int assetId,
                      float initialPrice);
        void startReadLoop();
        ~TopicListener() = default;
    };

} // namespace MarketExecution
