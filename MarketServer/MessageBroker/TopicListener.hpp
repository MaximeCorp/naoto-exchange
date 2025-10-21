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

#include "../MarketExecution/BidAsk/BidAsk.hpp"

namespace MarketExecution
{
    class TopicListener
    {
    private:
        std::string Topic;
        BidAsk AssetMarket;

        std::mutex AssetMarketMutex;

        Order parseOrder(std::string command);
        void startReadLoop();

    public:
        TopicListener(std::string topic, int totalSupply, int assetId,
                      float initialPrice);
        ~TopicListener() = default;
    };

} // namespace MarketExecution
