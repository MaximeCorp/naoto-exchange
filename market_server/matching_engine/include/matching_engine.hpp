#pragma once

#include <bid_ask.hpp>
#include <cassert>
#include <consumer.hpp>
#include <cstdlib>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <market_data_emitters.hpp>
#include <matching_engine_types.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <order_book_update.hpp>
#include <order_ingress_server.hpp>
#include <order_state_report.hpp>
#include <pthread.h>
#include <shared_memory_ops.hpp>
#include <shared_memory_types.hpp>
#include <spsc_queue.hpp>
#include <string>
#include <system_conf.hpp>
#include <thread>

namespace naoto::matching_engine
{
    class MatchingEngine
    {
    private:
#ifndef NAOTO_SHARED_MEMORY
        OrderBatchQueue IncomingOrders;
#endif
        OrderStateQueue OutgoingOrders;
        OrderBookUpdateQueue OutgoingBook;
#ifndef NAOTO_SHARED_MEMORY
        OrderBatchMempool OrdersPool;
#endif
        OrderStateMempool OrderStatesPool;
        OrderBookUpdateMempool OrderBookUpdatesPool;
        BidAsk OrderBook;
#ifndef NAOTO_SHARED_MEMORY
        OrderIngressServer Server;
#endif
        MarketDataEmitters Emitters;

        std::shared_ptr<etcd::KeepAlive> KeepAlive;
        std::unique_ptr<etcd::SyncClient> EtcdClient;

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

        void EtcdClientSetUp(void)
        {
            const char *etcd_addr =
                std::getenv("ETCD_ADDR") ?: "localhost:2379";
            const char *symbol = std::getenv("SYMBOL") ?: "1";
            const char *listen = std::getenv("LISTEN_ADDR") ?: "127.0.0.1:8080";

            EtcdClient = std::make_unique<etcd::SyncClient>(etcd_addr);

            KeepAlive = EtcdClient->leasekeepalive(10);
            int64_t lid = KeepAlive->Lease();

            std::string key = std::string("/matching-engines/") + symbol;

            EtcdClient->set(key,
                            nlohmann::json({ { "addr", listen },
                                             { "asset_id", std::atoi(symbol) },
                                             { "status", "active" } })
                                .dump(),
                            lid);
        }

    public:
        MatchingEngine(int argc, char **argv, const std::int32_t assetId,
                       const std::int64_t initialPrice,
#ifndef NAOTO_SHARED_MEMORY
                       const int port, const int maxEvents,
                       const int maxPending,
#endif
                       const size_t nb_fds, const uint16_t portId,
                       const uint16_t nbTxQueueSlots, const size_t poolSize,
                       const uint32_t srcIp, const uint16_t srcPort,
                       const uint32_t dstOrderIp, const uint16_t dstOrderPort,
                       const uint32_t dstBookIp, const uint16_t dstBookPort)
            : OrderBook(assetId, initialPrice,
#ifdef NAOTO_SHARED_MEMORY
                        CreateSharedQueue(),
#else
                        &IncomingOrders,
#endif
                        &OutgoingOrders, &OutgoingBook,
#ifndef NAOTO_SHARED_MEMORY
                        OrdersPool,
#endif
                        OrderStatesPool, OrderBookUpdatesPool)
#ifndef NAOTO_SHARED_MEMORY
            , Server(port, maxEvents, maxPending, OrdersPool, &IncomingOrders,
                     nb_fds)
#endif
            , Emitters(argc, argv, &OutgoingOrders, &OutgoingBook,
                       OrderStatesPool, OrderBookUpdatesPool, portId,
                       nbTxQueueSlots, poolSize, srcIp, srcPort, dstOrderIp,
                       dstOrderPort, dstBookIp, dstBookPort)
        {
            FileDescriptorsOps::setMaxFd(nb_fds);
        }

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

            EtcdClientSetUp();

            std::thread matchingThread(&BidAsk::MarketExecutionLoop,
                                       &OrderBook);

#ifndef NAOTO_SHARED_MEMORY
            std::thread serverThread(&OrderIngressServer::startServer, &Server);
#endif

            setAffinity(matchingThread, 8);
#ifndef NAOTO_SHARED_MEMORY
            setAffinity(serverThread, 6);
#endif

            pthread_setname_np(matchingThread.native_handle(),
                               "MatchineEngine");

#ifndef NAOTO_SHARED_MEMORY
            pthread_setname_np(serverThread.native_handle(), "EpollServer");
#endif

            Emitters.StartEmittersLoop();

            matchingThread.join();
#ifndef NAOTO_SHARED_MEMORY
            serverThread.join();
#endif
        }
    };
} // namespace naoto::matching_engine
