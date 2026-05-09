#pragma once

#include <BidAsk.hpp>
#include <EpollServer.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <pthread.h>
#include <thread>

namespace MarketExecution
{
    template <size_t BatchSize, size_t SkipListMaxLevel, size_t FHMSize>
    class MatchingEngine
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        OrdersQueue IncomingOrders;
        OrdersQueue OutgoingOrders;
        StoragePool<OrderBatch<BatchSize>> OrdersPool;
        BidAsk<FHMSize, SkipListMaxLevel, BatchSize> OrderBook;
        EpollServer<BatchSize> Server;

        void setAffinity(std::thread &t, const int core_id)
        {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id, &cpuset);

            int rc = pthread_setaffinity_np(t.native_handle(),
                                            sizeof(cpu_set_t), &cpuset);

            if (rc != 0)
            {
                std::cerr << "Error setting affinity: " << rc << std::endl;
            }
            else
            {
                std::cout << "Thread successfully pinned to CPU " << core_id
                          << std::endl;
            }
        }

    public:
        MatchingEngine(const size_t queueSize,
                       const size_t skipListNodesPoolSize,
                       const std::int32_t assetId,
                       const std::int64_t initialPrice, const int port,
                       const int maxEvents, const int maxPending,
                       const size_t nb_fds, const size_t orderNodePoolSize)
            : IncomingOrders(queueSize)
            , OutgoingOrders(queueSize)
            , OrdersPool(queueSize)
            , OrderBook(assetId, initialPrice, IncomingOrders, OutgoingOrders,
                        OrdersPool, orderNodePoolSize, skipListNodesPoolSize)
            , Server(port, maxEvents, maxPending, OrdersPool, IncomingOrders,
                     nb_fds)
        {}

        void StartMatchingEngine(void)
        {
            std::thread matchingThread(&BidAsk<FHMSize, SkipListMaxLevel,
                                               BatchSize>::MarketExecutionLoop,
                                       &OrderBook);
            std::thread serverThread(&EpollServer<BatchSize>::startServer,
                                     &Server);

            setAffinity(matchingThread, 5);
            setAffinity(serverThread, 6);

            pthread_setname_np(matchingThread.native_handle(),
                               "MatchineEngine");
            pthread_setname_np(serverThread.native_handle(), "EpollServer");

            matchingThread.join();
            serverThread.join();
        }
    };
} // namespace MarketExecution
