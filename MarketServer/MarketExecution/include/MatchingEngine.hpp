#pragma once

#include <BidAsk.hpp>
#include <EpollServer.hpp>
#include <MarketUpdates.hpp>
#include <OrderBookUpdate.hpp>
#include <OrderStateReport.hpp>
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
            ObjectBatch<Order, BatchSize> *>;
        using OrderStatesQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;
        using OrderBookUpdatesQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderBookUpdate *>;

    private:
        OrdersQueue IncomingOrders;
        OrderStatesQueue OutgoingOrders;
        OrderBookUpdatesQueue OutgoingBook;
        StoragePool<ObjectBatch<Order, BatchSize>> OrdersPool;
        StoragePool<OrderStateReport> OrderStatesPool;
        StoragePool<OrderBookUpdate> OrderBookUpdatesPool;
        BidAsk<FHMSize, SkipListMaxLevel, BatchSize, OrderMapSize> OrderBook;
        EpollServer<Order, BatchSize> Server;
        MarketUpdates<BatchSize> MarketUpdatesSender;

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
        MatchingEngine(int argc, char **argv, const size_t queueSize,
                       const size_t skipListNodesPoolSize,
                       const std::int32_t assetId,
                       const std::int64_t initialPrice, const int port,
                       const int maxEvents, const int maxPending,
                       const size_t nb_fds, const size_t orderNodePoolSize,
                       const uint16_t portId, const uint16_t nbTxQueueSlots,
                       const size_t poolSize, const uint32_t srcIp,
                       const uint16_t srcPort, const uint32_t dstOrderIp,
                       const uint16_t dstOrderPort, const uint32_t dstBookIp,
                       const uint16_t dstBookPort)
            : IncomingOrders(queueSize)
            , OutgoingOrders(queueSize)
            , OutgoingBook(queueSize)
            , OrdersPool(queueSize)
            , OrderStatesPool(queueSize)
            , OrderBookUpdatesPool(queueSize)
            , OrderBook(assetId, initialPrice, IncomingOrders, OutgoingOrders,
                        OutgoingBook, OrdersPool, OrderStatesPool,
                        OrderBookUpdatesPool, orderNodePoolSize,
                        skipListNodesPoolSize)
            , Server(port, maxEvents, maxPending, OrdersPool, IncomingOrders,
                     nb_fds)
            , MarketUpdatesSender(
                  argc, argv, OutgoingOrders, OutgoingBook, OrderStatesPool,
                  OrderBookUpdatesPool, portId, nbTxQueueSlots, poolSize, srcIp,
                  srcPort, dstOrderIp, dstOrderPort, dstBookIp, dstBookPort)
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
            assert(rte_lcore_id() == rte_get_main_lcore()
                   && "StartMatchingEngine must run on the thread that "
                      "constructed "
                      "MatchingEngine (the DPDK main lcore)");

            etcdClientSetUp();

            std::thread matchingThread(
                &BidAsk<FHMSize, SkipListMaxLevel, BatchSize,
                        OrderMapSize>::MarketExecutionLoop,
                &OrderBook);
            std::thread serverThread(
                &EpollServer<Order, BatchSize>::startServer, &Server);

            setAffinity(matchingThread, 5);
            setAffinity(serverThread, 6);

            pthread_setname_np(matchingThread.native_handle(),
                               "MatchineEngine");
            pthread_setname_np(serverThread.native_handle(), "EpollServer");

            MarketUpdatesSender.StartEmittersLoop();

            matchingThread.join();
            serverThread.join();
        }
    };
} // namespace MarketExecution
