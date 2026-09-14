#pragma once

#include <arpa/inet.h>
#include <array>
#include <bytes_buffer.hpp>
#include <cerrno>
#include <client_states.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <etcd/Watcher.hpp>
#include <gateway_handshake.hpp>
#include <iostream>
#include <local_attempts.hpp>
#include <memory>
#include <netdb.h>
#include <netinet/tcp.h>
#include <nlohmann/json.hpp>
#include <order.hpp>
#include <order_confirmation.hpp>
#include <order_gateway_types.hpp>
#include <order_risk_check.hpp>
#include <shared_memory_types.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <string>
#include <system_conf.hpp>
#include <unistd.h>
#include <vector>
#include <versioned_fd.hpp>

#ifdef NAOTO_PERF
#    include <timestamps.hpp>
#endif

namespace naoto::order_gateway
{
    class OrderRouter
    {
    private:
        ClientStates
            &clientStates; // Only for read (another object will write in it)
        std::array<LocalAttempts, GatewayMaxClients> LocalAttempt;
        std::array<uint32_t, GatewayMaxClients> LocalSessionId;

        OrderBatchMempool &OrdersPool;
#ifdef NAOTO_SHARED_MEMORY
        OrderProducer OutgoingOrder;
#else
        std::array<VersionedFd, MaxAssets> MatchingEngines;
        std::array<OrderResendBuffer, MaxAssets> OrderBuffer;
        size_t OrderOffset;
#endif
        std::array<ConfirmationResendBuffer, GatewayMaxClients>
            ConfirmationBuffer;
        VersionedFd &AccountFd;
        std::shared_ptr<etcd::KeepAlive> KeepAlive;
        std::unique_ptr<etcd::SyncClient> EtcdClient;
        std::unique_ptr<etcd::Watcher> EtcdMEWatcher;
        std::unique_ptr<etcd::Watcher> EtcdAccountServiceWatcher;
        OrderBatchConsumer Orders;
        const uint16_t GatewayId;
        uint64_t OrdersCount;
        OrderConfirmationProducer OutgoingConfirmation;

        [[nodiscard]] int32_t connectToService(const std::string &service_addr)
        {
            auto colon = service_addr.rfind(':');
            if (colon == std::string::npos)
            {
                std::cerr << "Invalid address: " << service_addr << "\n";
                return -1;
            }

            std::string ip = service_addr.substr(0, colon);
            int port = std::stoi(service_addr.substr(colon + 1));

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

            std::cout << "connecting to " << service_addr << "\n";

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            {
                if (errno != EINPROGRESS)
                {
                    std::cerr << "Failed to connect to " << ip << ":" << port
                              << ".\n";
                    close(fd);
                    return -1;
                }
            }

            return fd;
        }

#ifndef NAOTO_SHARED_MEMORY
        void etcdOnMEResponse(etcd::Response &resp) noexcept
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
                    const std::string service_addr = val["addr"];
                    uint32_t symbol_id = val["asset_id"].get<uint32_t>();

                    int32_t newFd = connectToService(
                        service_addr); // Arbitrary port value for now

                    VersionedFd &curSlot = MatchingEngines[symbol_id];

                    std::cout
                        << "new matching engine found: asset id = " << symbol_id
                        << "\n";

                    int32_t oldFd = VersionedFd::Fd(
                        curSlot.load(std::memory_order_acquire));

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

                    VersionedFd &curSlot = MatchingEngines[symbol_id];

                    int32_t oldFd = VersionedFd::Fd(
                        curSlot.load(std::memory_order_acquire));

                    curSlot.SwitchFd(-1);

                    if (oldFd != -1)
                    {
                        close(oldFd);
                    }
                }
            }
        }
#endif

        void etcdOnAccountServiceResponse(etcd::Response &resp) noexcept
        {
            const char *machine_id = std::getenv("MACHINE_ID") ?: "0";

            if (resp.error_code() != 0)
            {
                return;
            }

            for (auto &ev : resp.events())
            {
                if (ev.event_type() == etcd::Event::EventType::PUT)
                {
                    auto val = nlohmann::json::parse(ev.kv().as_string());
                    const std::string account_addr = val["addr"];

                    int32_t newFd = connectToService(
                        account_addr); // Arbitrary port value for now

                    GatewayHandshake handshake;
                    handshake.GatewayId = std::stoi(machine_id);

                    size_t totalSent = 0;

                    while (totalSent < sizeof(GatewayHandshake))
                    {
                        ssize_t sent = send(
                            newFd, (uint8_t *)(&handshake) + totalSent,
                            sizeof(GatewayHandshake) - totalSent, MSG_NOSIGNAL);

                        if (sent <= 0)
                        {
                            if (errno == EINTR)
                            {
                                continue;
                            }
                            std::cerr << "Failed sending first message to "
                                         "account service\n\n";
                            return;
                        }

                        totalSent += sent;
                    }

                    std::cout << "new account service found:  " << account_addr
                              << "\n";

                    int32_t oldFd = VersionedFd::Fd(
                        AccountFd.load(std::memory_order_acquire));

                    AccountFd.SwitchFd(newFd);

                    if (oldFd != -1)
                    {
                        std::cout
                            << "New account service: closing connection to "
                               "old account service.\n\n";
                        close(oldFd);
                    }
                }
                else if (ev.event_type() == etcd::Event::EventType::DELETE_)
                {
                    std::string key = ev.kv().key();

                    int32_t oldFd = VersionedFd::Fd(
                        AccountFd.load(std::memory_order_acquire));

                    AccountFd.SwitchFd(-1);

                    if (oldFd != -1)
                    {
                        std::cout << "Delete account service: closing "
                                     "connection to account service.\n\n";
                        close(oldFd);
                    }
                }
            }
        }

        void EtcdClientSetUp(void)
        {
            const char *etcd_addr =
                std::getenv("ETCD_ADDR") ?: "http://localhost:2379";
            const char *machine_id = std::getenv("MACHINE_ID") ?: "0";
            const char *listen = std::getenv("LISTEN_ADDR") ?: "localhost:8081";

            EtcdClient = std::make_unique<etcd::SyncClient>(etcd_addr);

            KeepAlive = EtcdClient->leasekeepalive(10);
            int64_t lid = KeepAlive->Lease();

            std::cerr << "Got lease ID: " << lid << "\n";

            std::string key = std::string("/socket-gateways/") + machine_id;

            EtcdClient->set(
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

#ifndef NAOTO_SHARED_MEMORY
            auto existingME = EtcdClient->ls("/matching-engines/");
            for (auto &kv : existingME.values())
            {
                auto val = nlohmann::json::parse(kv.as_string());
                const std::string curAddr = val["addr"];
                uint32_t symbol_id = val["asset_id"].get<uint32_t>();

                std::cout << "Found matching engine " << symbol_id << "\n";
                int32_t fd = connectToService(curAddr);
                if (fd != -1)
                {
                    MatchingEngines[symbol_id].SwitchFd(fd);
                }
            }

            int64_t revisionME = existingME.index();
#endif

            GatewayHandshake handshake;
            handshake.GatewayId = std::stoi(machine_id);

            auto existingAccountService = EtcdClient->ls("/account-service/");
            for (auto &kv : existingAccountService.values())
            {
                auto val = nlohmann::json::parse(kv.as_string());
                const std::string curAddr = val["addr"];

                std::cout << "Found account service\n";

                int32_t fd = connectToService(curAddr);

                size_t totalSent = 0;

                while (totalSent < sizeof(GatewayHandshake))
                {
                    ssize_t sent = send(fd, (uint8_t *)(&handshake) + totalSent,
                                        sizeof(GatewayHandshake) - totalSent,
                                        MSG_NOSIGNAL);

                    if (sent <= 0)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        std::cerr << "Failed sending first message to "
                                     "account service\n\n";
                        return;
                    }

                    totalSent += sent;
                }

                if (fd != -1)
                {
                    AccountFd.SwitchFd(fd);
                }
            }

            int64_t revisionAccountService = existingAccountService.index();

#ifndef NAOTO_SHARED_MEMORY
            std::string engine_addr;
            EtcdMEWatcher = std::make_unique<etcd::Watcher>(
                *EtcdClient, "/matching-engines/", revisionME + 1,
                [this](etcd::Response resp) { this->etcdOnMEResponse(resp); },
                true);
#endif

            EtcdAccountServiceWatcher = std::make_unique<etcd::Watcher>(
                *EtcdClient, "/account-service/", revisionAccountService + 1,
                [this](etcd::Response resp) {
                    this->etcdOnAccountServiceResponse(resp);
                },
                true);
        }

        template <typename T>
        [[nodiscard]] bool DrainBuffer(
            BytesBuffer<GatewayEpollReceiveBatchSize * sizeof(T)> &buffer,
            const uint32_t fd) noexcept
        {
            size_t totalSent = 0;

#ifndef NAOTO_SHARED_MEMORY
            if constexpr (std::is_same_v<T, Order>)
            {
                totalSent = OrderOffset;
            }
#endif

            const uint8_t *data = buffer.GetData();

            const size_t bufferSize = buffer.GetSize();

            while (totalSent < bufferSize)
            {
                ssize_t sent = send(fd, data + totalSent,
                                    bufferSize - totalSent, MSG_NOSIGNAL);

                totalSent += sent;

                if (sent <= 0) [[unlikely]]
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

#ifndef NAOTO_SHARED_MEMORY
                    if constexpr (std::is_same_v<T, Order>)
                    {
                        auto [sentOrders, sentBytes] =
                            std::div((int)(totalSent), (int)sizeof(Order));

                        buffer.Shift(sentOrders * sizeof(Order));
                        OrderOffset = sentBytes;
                    }
#endif
                    if constexpr (std::is_same_v<T, OrderConfirmation>)
                    {
                        buffer.Shift(totalSent);
                    }

                    return false;
                }
            }

            buffer.Clear();

#ifndef NAOTO_SHARED_MEMORY
            if constexpr (std::is_same_v<T, Order>)
            {
                OrderOffset = 0;
            }
#endif

            return true;
        }

#ifndef NAOTO_SHARED_MEMORY
        [[nodiscard]] bool SendOrder(const Order &curOrder,
                                     const uint64_t curVal) noexcept
        {
            int32_t curFd = VersionedFd::Fd(curVal);

            // edge case: if fd gets closed then recycled by and the
            // new fd is for client, then information leak, handle
            // this by closing fd after making sure the sender has
            // seen the new fd

            size_t totalSent = 0;

            while (totalSent < sizeof(Order))
            {
                ssize_t sent = send(curFd, (uint8_t *)(&curOrder) + totalSent,
                                    sizeof(Order) - totalSent, MSG_NOSIGNAL);

                if (sent <= 0) [[unlikely]]
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

                    OrderBuffer[curOrder.AssetId].Add((uint8_t *)&curOrder,
                                                      sizeof(Order));
                    OrderOffset = totalSent;

                    return false;
                }

                totalSent += sent;
            }
            // edge case: send < sizeof(Order)

            VersionedFd &curSlot = MatchingEngines[curOrder.AssetId];

            uint64_t newVal = curSlot.load(std::memory_order_acquire);

            if (newVal != curVal) [[unlikely]]
            {
                return false;
            }

            return true;
        }
#endif

        void SendOrderConfirmation(const OrderConfirmation *confirmation,
                                   const uint32_t fd) noexcept
        {
            size_t totalSent = 0;

            while (totalSent < sizeof(OrderConfirmation))
            {
                ssize_t sent =
                    send(fd, (uint8_t *)(confirmation) + totalSent,
                         sizeof(OrderConfirmation) - totalSent, MSG_NOSIGNAL);

                if (sent <= 0) [[unlikely]]
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

                    ConfirmationBuffer[fd].Add(
                        (uint8_t *)(confirmation) + totalSent,
                        sizeof(OrderConfirmation) - totalSent);

                    return;
                }

                totalSent += sent;
            }
        }

        // TODO: Make it return OrderConfirmationStatus
        [[nodiscard]] OrderConfirmationStatus
        CheckOrderRisk(const uint32_t fd, const Order &order,
                       const uint8_t auth) noexcept
        {
            if (!auth) [[unlikely]]
            {
                return OrderConfirmationStatus::UserNotConnected;
            }

            ClientState curState = clientStates.GetClientState(fd);

            OrderConfirmationStatus status =
                naoto::order_gateway::CheckOrderRisk(curState, LocalAttempt[fd],
                                                     LocalSessionId[fd], order,
                                                     auth);

            if (status == OrderConfirmationStatus::BadClientId) [[unlikely]]
            {
                ConfirmationBuffer[fd].Clear();
            }

            return status;
        }

        void consumeOrder(void) noexcept
        {
            OrderBatch *curBatch = nullptr;

            // TODO : find a way to clear confirmation resend buffer when new
            // connection comes
            if (Orders.TryPop(curBatch)) [[likely]]
            {
                const uint32_t curFd = curBatch->Fd;

                for (size_t i = 0; i < curBatch->Size; ++i)
                {
                    Order &curOrder = (*curBatch)[i];

#ifndef NAOTO_SHARED_MEMORY
                    VersionedFd &curSlot = MatchingEngines[curOrder.AssetId];
                    uint64_t curVal = curSlot.load(std::memory_order_acquire);
#endif
                    OrderConfirmation curConfirmation;

#ifndef NAOTO_SHARED_MEMORY
                    if (VersionedFd::Fd(curVal) == -1) [[unlikely]]
                    {
                        curConfirmation.Status =
                            OrderConfirmationStatus::TechnicalFailure;
                    }
                    else
                    {
#endif
                        curConfirmation.Status =
                            CheckOrderRisk(curFd, curOrder, curBatch->Auth);
#ifndef NAOTO_SHARED_MEMORY
                    }
#endif

                    constexpr uint64_t COUNTER_MASK = (1ULL << 48) - 1;

                    curConfirmation.OrderId = ((uint64_t)(GatewayId) << 48)
                        | (OrdersCount++ & COUNTER_MASK);
                    curConfirmation.ClientOrderId = curOrder.ClientOrderId;

                    curOrder.OrderId = curConfirmation.OrderId;

                    if (curConfirmation.Status
                        == OrderConfirmationStatus::Accepted) [[likely]]
                    {
#ifndef NAOTO_SHARED_MEMORY
                        bool drained = DrainBuffer<Order>(
                            OrderBuffer[curOrder.AssetId], curFd);

                        if (!drained) [[unlikely]]
                        {
                            if (!OrderBuffer[curOrder.AssetId].CanAdd(
                                    sizeof(Order))) [[unlikely]]
                            {
                                // Something must be wrong with the connection
                                continue;
                            }

                            OrderBuffer[curOrder.AssetId].Add(
                                (uint8_t *)&curOrder, sizeof(Order));
                            continue;
                        }
#endif

#ifdef NAOTO_PERF
                        curOrder.RoutedTimestamp = now_tsc();
#endif

#ifdef NAOTO_SHARED_MEMORY
                        if (OutgoingOrder.TryPush(curOrder))
                        {
#else
                        if (SendOrder(curOrder, curVal))
                        {
#endif
                            ClientMessage message{ curFd, curConfirmation };
                            OutgoingConfirmation.Push(message);
                        }
                    }
                    else
                    {
                        ClientMessage message{ curFd, curConfirmation };
                        OutgoingConfirmation.Push(message);
                    }
                }

                if (!OrdersPool.Release(curBatch))
                {
                    perror("Failed mempool release.\n");
                }
            }
        }

    public:
        OrderRouter(OrderBatchMempool &ordersPool, OrderBatchQueue *orders,
                    VersionedFd &accountFd, ClientStates &clientStates,
                    OrderConfirmationQueue *outgoingConfirmation
#ifdef NAOTO_SHARED_MEMORY
                    ,
                    OrderQueue *outgoingOrder
#endif
                    )
            : clientStates(clientStates)
            , OrdersPool(ordersPool)
#ifdef NAOTO_SHARED_MEMORY
            , OutgoingOrder(outgoingOrder)
#else
            , OrderOffset(0)
#endif
            , AccountFd(accountFd)
            , Orders(orders)
            , GatewayId(std::stoi(std::getenv("MACHINE_ID") ?: "0"))
            , OrdersCount(std::stoi(std::getenv("ORDERS_COUNT") ?: "0"))
            , OutgoingConfirmation(outgoingConfirmation)
        {
            EtcdClientSetUp();
            LocalSessionId.fill(0);
        }

        ~OrderRouter()
        {
            if (EtcdMEWatcher)
            {
                EtcdMEWatcher->Cancel();
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
        }
    };
} // namespace naoto::order_gateway
