#pragma once

#include <BidAsk.hpp>
#include <EpollServer.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <cstdlib>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <nlohmann/json.hpp>
#include <pthread.h>
#include <string>
#include <thread>

namespace MarketExecution
{
    template <size_t BatchSize, size_t SkipListMaxLevel, size_t FHMSize,
              size_t OrderMapSize>
    class MatchingEngine
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        OrdersQueue IncomingOrders;
        OrdersQueue OutgoingOrders;
        StoragePool<OrderBatch<BatchSize>> OrdersPool;
        BidAsk<FHMSize, SkipListMaxLevel, BatchSize, OrderMapSize> OrderBook;
        EpollServer<BatchSize> Server;

        std::shared_ptr<etcd::KeepAlive> KeepAlive;
        std::unique_ptr<etcd::SyncClient> etcdClient;

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

        void etcdClientSetUp(void)
        {
            const char *etcd_addr =
                std::getenv("ETCD_ADDR") ?: "localhost:2379";
            const char *symbol = std::getenv("SYMBOL") ?: "0";
            const char *listen = std::getenv("LISTEN_ADDR") ?: "127.0.0.1:8080";

            etcdClient = std::make_unique<etcd::SyncClient>(etcd_addr);

            KeepAlive = etcdClient->leasekeepalive(10);
            int64_t lid = KeepAlive->Lease();

            std::string key = std::string("/matching-engines/") + symbol;

            etcdClient->set(key,
                            nlohmann::json({ { "addr", listen },
                                             { "asset_id", std::atoi(symbol) },
                                             { "status", "active" } })
                                .dump(),
                            lid);
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

        ~MatchingEngine()
        {
            if (KeepAlive)
            {
                KeepAlive->Cancel();
            }
        }

        void StartMatchingEngine(void)
        {
            etcdClientSetUp();

            std::thread matchingThread(
                &BidAsk<FHMSize, SkipListMaxLevel, BatchSize,
                        OrderMapSize>::MarketExecutionLoop,
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
