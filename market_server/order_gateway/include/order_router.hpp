#pragma once

#include <arpa/inet.h>
#include <array>
#include <bytes_buffer.hpp>
#include <client_states.hpp>
#include <cstdint>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <etcd/Watcher.hpp>
#include <versioned_fd.hpp>
#include <gateway_handshake.hpp>
#include <local_attempts.hpp>
#include <netdb.h>
#include <netinet/tcp.h>
#include <nlohmann/json.hpp>
#include <order.hpp>
#include <order_confirmation.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>
#include <vector>

namespace naoto::order_gateway
{
    template <size_t BatchSize, size_t MaxAsset, size_t MaxPositions,
              size_t MaxClients>
    class OrderRouter
    {
        using OrderQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;

    private:
        ClientStates<MaxPositions>
            &clientStates; // Only for read (another object will write in it)
        std::array<LocalAttempts<MaxPositions>, MaxClients> LocalAttempt;
        std::array<uint32_t, MaxClients> LocalSessionId;

        StoragePool<ObjectBatch<Order, BatchSize>> &OrdersPool;
        std::array<VersionedFd, MaxAsset> MatchingEngines;
        std::array<BytesBuffer<BatchSize * sizeof(Order)>, MaxAsset>
            OrderBuffer;
        size_t OrderOffset;

        std::array<BytesBuffer<BatchSize * sizeof(OrderConfirmation)>,
                   MaxClients>
            ConfirmationBuffer;
        VersionedFd &AccountFd;
        OrderQueue &Orders;
        std::shared_ptr<etcd::KeepAlive> KeepAlive;
        std::unique_ptr<etcd::SyncClient> EtcdClient;
        std::unique_ptr<etcd::Watcher> EtcdMEWatcher;
        std::unique_ptr<etcd::Watcher> EtcdAccountServiceWatcher;
        const uint16_t GatewayId;
        uint64_t OrdersCount;

        [[nodiscard]] int32_t
        connectToService(const std::string &service_addr)
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
                        ssize_t sent =
                            send(newFd, (uint8_t *)(&handshake) + totalSent,
                                 sizeof(GatewayHandshake) - totalSent,
                                 MSG_NOSIGNAL);

                        if (sent <= 0)
                        {
                            if (errno == EINTR)
                            {
                                continue;
                            }
                            std::cerr
                                << "Failed sending first message to "
                                  "account service\n\n";
                            return;
                        }

                        totalSent += sent;
                    }

                    std::cout
                        << "new account service found:  " << account_addr
                        << "\n";

                    int32_t oldFd =
                        VersionedFd::Fd(
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

                    int32_t oldFd =
                        VersionedFd::Fd(
                            AccountFd.load(std::memory_order_acquire));

                    AccountFd.SwitchFd(-1);

                    if (oldFd != -1)
                    {
                        std::cout
                            << "Delete account service: closing "
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
                    ssize_t sent = send(
                        fd, (uint8_t *)(&handshake) + totalSent,
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

                if (fd != -1)
                {
                    AccountFd.SwitchFd(fd);
                }
            }

            int64_t revisionAccountService = existingAccountService.index();

            std::string engine_addr;
            EtcdMEWatcher = std::make_unique<etcd::Watcher>(
                *EtcdClient, "/matching-engines/", revisionME + 1,
                [this](etcd::Response resp) { this->etcdOnMEResponse(resp); },
                true);

            EtcdAccountServiceWatcher = std::make_unique<etcd::Watcher>(
                *EtcdClient, "/account-service/", revisionAccountService + 1,
                [this](etcd::Response resp) {
                    this->etcdOnAccountServiceResponse(resp);
                },
                true);
        }

        template <typename T>
        [[nodiscard]] bool
        DrainBuffer(BytesBuffer<BatchSize * sizeof(T)> &buffer,
                    const uint32_t fd) noexcept
        {
            size_t totalSent = 0;

            if constexpr (std::is_same_v<T, Order>)
            {
                totalSent = OrderOffset;
            }

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

                    if constexpr (std::is_same_v<T, Order>)
                    {
                        auto [sentOrders, sentBytes] =
                            std::div((int)(totalSent), (int)sizeof(Order));

                        buffer.Shift(sentOrders * sizeof(Order));
                        OrderOffset = sentBytes;
                    }
                    else if constexpr (std::is_same_v<T, OrderConfirmation>)
                    {
                        buffer.Shift(totalSent);
                    }

                    return false;
                }
            }

            buffer.Clear();

            if constexpr (std::is_same_v<T, Order>)
            {
                OrderOffset = 0;
            }

            return true;
        }

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

                std::cerr << "Sending order to fd " << curFd
                          << ", size=" << sizeof(Order) << ", sent=" << sent
                          << "\n";

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
                std::cerr
                    << "Order refused: Client was not authentificated yet\n\n";

                return OrderConfirmationStatus::UserNotConnected;
            }

            ClientState<MaxPositions> curState =
                clientStates.GetClientState(fd);

            std::cout << "Risk check going on:\n";
            curState.log();

            if (curState.SessionId != LocalSessionId[fd]) [[unlikely]]
            {
                LocalAttempt[fd].Clear();
                LocalSessionId[fd] = curState.SessionId;
            }

            if (curState.ClientId != order.ClientId) [[unlikely]]
            {
                ConfirmationBuffer[fd].Clear();

                std::cout << "ClientId mismatch.\n\n";
                return OrderConfirmationStatus::BadClientId;
            }

            uint16_t assetId =
                order.Type == OrderType::MARKET && order.Side == OrderSide::BUY
                ? 0
                : order.AssetId;

            size_t assetIdx;

            bool found = curState.GetAssetIdx(assetId, assetIdx);

            if (!found) [[unlikely]]
            {
                // Think about how to handle missing assetId
                // Should evict an asset that has attempt = 0
                std::cerr << "Requested asset id not in the client state\n\n";

                return assetId >= MaxAsset
                    ? OrderConfirmationStatus::UnknownSymbol
                    : OrderConfirmationStatus::MaxPositions;
            }

            int64_t confirmed = curState.GetConfirmedAt(assetIdx);
            int64_t attempt =
                curState.GetAttemptAt(assetIdx) + LocalAttempt[fd][assetIdx];

            // The maximum needed amount when selling is the amount since it's
            // exactly what we'll spend
            // For buy, LIMIT order allows us to calculate exactly how much will
            // be spent and MARKET orders have a maximum price, giving us an
            // upper bound which we will use to freeze money

            int64_t amount = order.Side == OrderSide::SELL
                ? order.Amount
                : order.Amount * order.Price;

            std::cout << "Trying to use " << amount << " of asset " << assetId
                      << ", " << confirmed - attempt << " available ("
                      << LocalAttempt[fd][assetIdx]
                      << " from local counter).\n\n";

            if (amount > confirmed - attempt) [[unlikely]]
            {
                std::cerr << "Trade rejected: not enough funds\n\n";
                return OrderConfirmationStatus::InsufficientFunds;
            }

            // TODO : add the session gen counter to detect new
            // connections and reset local counter
            LocalAttempt[fd][assetIdx] += amount;

            return OrderConfirmationStatus::Accepted;
        }

        void consumeOrder(void) noexcept
        {
            ObjectBatch<Order, BatchSize> *curBatch = nullptr;

            // TODO : find a way to clear confirmation resend buffer when new
            // connection comes
            if (Orders.try_dequeue(curBatch)) [[likely]]
            {
                std::cout << "Received order batch of size " << curBatch->Size
                          << " at risk service\n\n";

                const uint32_t curFd = curBatch->Fd;

                for (size_t i = 0; i < curBatch->Size; ++i)
                {
                    std::cout << "Risk checking an order\n";

                    Order &curOrder = (*curBatch)[i];

                    curOrder.log();

                    VersionedFd &curSlot = MatchingEngines[curOrder.AssetId];
                    uint64_t curVal = curSlot.load(std::memory_order_acquire);

                    OrderConfirmation curConfirmation;

                    if (VersionedFd::Fd(curVal) == -1) [[unlikely]]
                    {
                        std::cout << "no matching engine at asset id "
                                  << curOrder.AssetId << "\n";
                        curConfirmation.Status =
                            OrderConfirmationStatus::TechnicalFailure;
                    }
                    else
                    {
                        curConfirmation.Status =
                            CheckOrderRisk(curFd, curOrder, curBatch->Auth);
                    }

                    constexpr uint64_t COUNTER_MASK = (1ULL << 48) - 1;

                    curConfirmation.OrderId = ((uint64_t)(GatewayId) << 48)
                        | (OrdersCount++ & COUNTER_MASK);
                    curConfirmation.ClientOrderId = curOrder.ClientOrderId;

                    curOrder.OrderId = curConfirmation.OrderId;

                    if (curConfirmation.Status
                        == OrderConfirmationStatus::Accepted) [[likely]]
                    {
                        std::cout
                            << "Order Accepted, sending to matching engine\n\n";

                        bool drained = DrainBuffer<Order>(
                            OrderBuffer[curOrder.AssetId], curFd);

                        if (!drained) [[unlikely]]
                        {
                            std::cout << "Failed draining orders resend buffer "
                                         "before sending order\n\n";

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

                        if (SendOrder(curOrder, curVal))
                        {
                            drained = DrainBuffer<OrderConfirmation>(
                                ConfirmationBuffer[curFd], curFd);

                            if (!drained) [[unlikely]]
                            {
                                if (ConfirmationBuffer[curFd].CanAdd(
                                        sizeof(OrderConfirmation))) [[unlikely]]
                                {
                                    ConfirmationBuffer[curFd].Add(
                                        (uint8_t *)&curConfirmation,
                                        sizeof(OrderConfirmation));
                                }
                            }
                            else
                            {
                                SendOrderConfirmation(&curConfirmation, curFd);
                            }
                        }
                    }
                    else
                    {
                        std::cout << "Order Rejected\n\n";

                        bool drained = DrainBuffer<OrderConfirmation>(
                            ConfirmationBuffer[curFd], curFd);

                        if (!drained) [[unlikely]]
                        {
                            if (ConfirmationBuffer[curFd].CanAdd(
                                    sizeof(OrderConfirmation))) [[unlikely]]
                            {
                                ConfirmationBuffer[curFd].Add(
                                    (uint8_t *)&curConfirmation,
                                    sizeof(OrderConfirmation));
                            }
                        }
                        else
                        {
                            SendOrderConfirmation(&curConfirmation, curFd);
                        }
                    }
                }

                if (!OrdersPool.release(curBatch))
                {
                    perror("Failed mempool release.\n");
                }
            }
        }

    public:
        OrderRouter(StoragePool<ObjectBatch<Order, BatchSize>> &ordersPool,
                    OrderQueue &orders, VersionedFd &accountFd,
                    ClientStates<MaxPositions> &clientStates)
            : clientStates(clientStates)
            , OrdersPool(ordersPool)
            , OrderOffset(0)
            , AccountFd(accountFd)
            , Orders(orders)
            , GatewayId(std::stoi(std::getenv("MACHINE_ID") ?: "0"))
            , OrdersCount(std::stoi(std::getenv("ORDERS_COUNT") ?: "0"))
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
