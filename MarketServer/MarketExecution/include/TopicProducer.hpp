#pragma once

#include <Order.hpp>
#include <StoragePool.hpp>
#include <array>
#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <rdkafkacpp.h>
#include <thread>

#include "../include/Order.hpp"

namespace MarketExecution
{
    class TopicProducer
    {
    private:
        RdKafka::Topic *Topic;
        RdKafka::Producer *Producer;
        StoragePool<Order> *OrderPool;
        StoragePool<std::array<char, sizeof(Order)>> *BinaryPool;

        void produceOrder(Order *order);

    public:
        TopicProducer(std::string brokers, std::string topic,
                      StoragePool<Order> *orderPool,
                      StoragePool<std::array<char, sizeof(Order)>> *binaryPool);

        void
        startConsumingStatusQueue(boost::lockfree::queue<Order *> &OrdersQueue,
                                  std::atomic<bool> &running);
    };
} // namespace MarketExecution
