#pragma once

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
    class ProducerEventCb : public RdKafka::EventCb
    {
    public:
        void event_cb(RdKafka::Event &event) override
        {
            if (event.type() == RdKafka::Event::EVENT_ERROR)
            {
                std::cerr << "ERROR: " << RdKafka::err2str(event.err()) << ": "
                          << event.str() << std::endl;
            }
        }
    };

    class TopicListener
    {
    private:
        std::string Topic;
        BidAsk AssetMarket;

        bool parseOrder(void *command, size_t n, Order *output);

    public:
        TopicListener(std::string topic, int totalSupply, int assetId,
                      float initialPrice);
        void startReadLoop();
        ~TopicListener() = default;
    };

} // namespace MarketExecution
