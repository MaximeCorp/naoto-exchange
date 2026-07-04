#pragma once

#include <ClientStates.hpp>
#include <FdGen.hpp>
#include <Order.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <etcd/Watcher.hpp>
#include <netdb.h>
#include <netinet/tcp.h>
#include <nlohmann/json.hpp>
#include <vector>

namespace Gateways
{
    template <size_t BatchSize, size_t MaxAsset, size_t MaxPositions>
    class RiskService
    {
        using OrderQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;

    private:
        ClientStates<MaxPositions>
            &clientStates; // Only for read (another object will write in it)

        StoragePool<ObjectBatch<Order, BatchSize>> &OrdersPool;
        std::array<FdGen, MaxAsset> MatchingEngines;
        OrderQueue &Orders;
        std::shared_ptr<etcd::KeepAlive> KeepAlive;
        std::unique_ptr<etcd::SyncClient> etcdClient;
        std::unique_ptr<etcd::Watcher> etcdWatcher;

        [[nodiscard]] int32_t
        connectMatchingEngines(const std::string &engine_addr)
        {
            auto colon = engine_addr.rfind(':');
            if (colon == std::string::npos)
            {
                std::cerr << "Invalid address: " << engine_addr << "\n";
                return -1;
            }

            std::string ip = engine_addr.substr(0, colon);
            int port = std::stoi(engine_addr.substr(colon + 1));

            int32_t fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

            if (fd < 0)
            {
                std::cerr << "Failed to create socket\n";
                return -1;
            }

            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            std::cout << "connecting to " << engine_addr << "\n";

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            {
                if (errno != EINPROGRESS)
                {
                    std::cerr << "Bro you didn't connect to matching engine "
                              << ip << ":" << port << ".\n";
                    close(fd);
                    return -1;
                }
            }

            return fd;
        }

        [[nodiscard]] int32_t connectMatchingEngines(const std::string &ip,
                                                     const int port)
        {
            int32_t fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

            if (fd < 0)
            {
                std::cerr << "Failed to create socket\n";
                return -1;
            }

            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            std::cout << "connecting to " << ip << "\n";

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            {
                if (errno != EINPROGRESS)
                {
                    std::cerr << "Bro you didn't connect to matching engine "
                              << ip << ":" << port << ".\n";
                    close(fd);
                    return -1;
                }
            }

            return fd;
        }

        void etcdOnResponse(etcd::Response &resp) noexcept
        {
            if (resp.error_code() != 0)
            {
                return;
            }

            for (auto &ev : resp.events())
            {
                if (ev.event_type() == etcd::Event::EventType::PUT)
                {
                    auto val = nlohmann::json::parse(ev.kv().as_string());
                    const std::string engine_addr = val["addr"];
                    uint32_t symbol_id = val["asset_id"].get<uint32_t>();

                    int32_t newFd = connectMatchingEngines(
                        engine_addr); // Arbitrary port value for now

                    FdGen &curSlot = MatchingEngines[symbol_id];

                    std::cout
                        << "new matching engine found: asset id = " << symbol_id
                        << "\n";

                    int32_t oldFd =
                        FdGen::Fd(curSlot.load(std::memory_order_relaxed));

                    curSlot.SwitchFd(newFd);

                    if (oldFd != -1)
                    {
                        close(oldFd);
                    }
                }
                else if (ev.event_type() == etcd::Event::EventType::DELETE_)
                {
                    std::string key = ev.kv().key();
                    uint32_t symbol_id = std::stoi(
                        key.substr(key.rfind('/')
                                   + 1)); // This needs to be changed after POC

                    FdGen &curSlot = MatchingEngines[symbol_id];

                    int32_t oldFd =
                        FdGen::Fd(curSlot.load(std::memory_order_relaxed));

                    curSlot.SwitchFd(-1);

                    if (oldFd != -1)
                    {
                        close(oldFd);
                    }
                }
            }
        }

        void etcdClientSetUp(void)
        {
            const char *etcd_addr =
                std::getenv("ETCD_ADDR") ?: "http://localhost:2379";
            const char *machine_id = std::getenv("MACHINE_ID") ?: "0";
            const char *listen = std::getenv("LISTEN_ADDR") ?: "localhost:8000";

            etcdClient = std::make_unique<etcd::SyncClient>(etcd_addr);

            KeepAlive = etcdClient->leasekeepalive(10);
            int64_t lid = KeepAlive->Lease();

            std::cerr << "Got lease ID: " << lid << "\n";

            std::string key = std::string("/socket-gateways/") + machine_id;

            etcdClient->set(
                key,
                nlohmann::json({ { "ip", listen }, { "status", "active" } })
                    .dump(),
                lid);

            if (!KeepAlive)
            {
                std::cerr << "leasekeepalive returned null!\n";
            }
            else
            {
                std::cerr << "KeepAlive active\n";
            }

            auto existing = etcdClient->ls("/matching-engines/");
            for (auto &kv : existing.values())
            {
                auto val = nlohmann::json::parse(kv.as_string());
                const std::string curAddr = val["addr"];
                uint32_t symbol_id = val["asset_id"].get<uint32_t>();

                std::cout << "Found matching engine " << symbol_id << "\n";
                int32_t fd = connectMatchingEngines(curAddr);
                if (fd != -1)
                {
                    MatchingEngines[symbol_id].SwitchFd(fd);
                }
            }

            int64_t revision = existing.index();

            std::string engine_addr;
            etcdWatcher = std::make_unique<etcd::Watcher>(
                *etcdClient, "/matching-engines/", revision + 1,
                [this](etcd::Response resp) { this->etcdOnResponse(resp); },
                true);
        }

        void consumeOrder(void) noexcept
        {
            ObjectBatch<Order, BatchSize> *curBatch = nullptr;

            if (Orders.try_dequeue(curBatch)) [[likely]]
            {
                std::cout << "Received order batch of size "
                          << curBatch->getSize() << " at risk service\n";

                for (size_t i = 0; i < curBatch->getSize(); ++i)
                {
                    const Order &curOrder = (*curBatch)[i];

                    curOrder.log();

                    if (clientStates.can_spend(curBatch->getFd(),
                                               curOrder.getAmount(),
                                               curOrder.getAsset())) [[likely]]
                    {
                        if (curOrder.getAsset() >= (int32_t)MaxAsset)
                            [[unlikely]]
                        {
                            std::cout << "refused because of max asset\n";
                            // Handle order rejection
                            continue;
                        }
                        FdGen &curSlot = MatchingEngines[curOrder.getAsset()];
                        uint64_t curVal =
                            curSlot.load(std::memory_order_relaxed);

                        std::cout << "(int32_t)curVal is " << (int32_t)curVal
                                  << "\n";

                        int32_t curFd = curVal;

                        if (curFd == -1) [[unlikely]]
                        {
                            std::cout << "no matching engine at asset id "
                                      << curOrder.getAsset() << "\n";
                            continue;
                        }

                        // edge case: if fd gets closed then recycled by and the
                        // new fd is for client, then information leak, handle
                        // this by closing fd after making sure the sender has
                        // seen the new fd

                        ssize_t sent =
                            send(curFd, &curOrder, sizeof(Order), MSG_NOSIGNAL);

                        std::cerr << "Sending order to fd " << curFd
                                  << ", size=" << sizeof(Order)
                                  << ", sent=" << sent << "\n";

                        if (sent < 0) [[unlikely]]
                        {
                            if (errno == EPIPE || errno == ECONNRESET)
                            {
                                // handle order rejection
                            }
                            // reject order
                            continue;
                        }
                        // edge case: send < sizeof(Order)

                        uint64_t newVal =
                            curSlot.load(std::memory_order_release);

                        if (newVal != curVal) [[unlikely]]
                        {
                            // Means the send was potentially sent to the wrong
                            // fd
                            // For later : push to the array / vector of
                            // messages to send again
                        }
                    }
                }

                if (!OrdersPool.release(curBatch))
                {
                    perror("Failed mempool release.\n");
                }
            }
            else
            {
                // Write error message to client fd
            }
        }

    public:
        RiskService(StoragePool<ObjectBatch<Order, BatchSize>> &ordersPool,
                    OrderQueue &orders,
                    ClientStates<MaxPositions> &clientStates)
            : clientStates(clientStates)
            , OrdersPool(ordersPool)
            , Orders(orders)
        {
            etcdClientSetUp();
        }

        ~RiskService()
        {
            if (etcdWatcher)
            {
                etcdWatcher->Cancel();
            }
            if (KeepAlive)
            {
                KeepAlive->Cancel();
            }
        }

        void startLoop(void)
        {
            while (true) // Maybe add ability to stop the loop
            {
                consumeOrder();
            }

            KeepAlive->Cancel();
        }
    };
} // namespace Gateways
