#pragma once

#include <atomic>
#include <client_auth_request.hpp>
#include <client_states.hpp>
#include <epoll_server.hpp>
#include <order.hpp>
#include <routed_auth_request.hpp>
#include <spsc_queue.hpp>
#include <string>
#include <system_conf.hpp>
#include <versioned_fd.hpp>

#ifdef NAOTO_PERF
#    include <timestamps.hpp>
#endif

namespace naoto::order_gateway
{
    template <size_t BatchSize, size_t MaxPositions>
    class GatewayServer
        : public EpollServer<GatewayServer<BatchSize, MaxPositions>, Order,
                             BatchSize, ClientAuthRequest>
    {
        using Base = EpollServer<GatewayServer<BatchSize, MaxPositions>, Order,
                                 BatchSize, ClientAuthRequest>;
        using DisconnectQueue = SpscQueueProducer<uint32_t, GatewayMaxClients>;
        using GatewayRequestQueue =
            SpscQueueProducer<RoutedAuthRequest *, GatewayMaxClients>;
        using OrderQueue = SpscQueue<ObjectBatch<Order, BatchSize> *,
                                     GatewayEpollReceiveQueueSize>;

    private:
        VersionedFd &AccountFd;
        uint16_t GatewayId;
        ClientStates<MaxPositions> &States;
        DisconnectQueue OutgoingDisconnects;
        StoragePool<RoutedAuthRequest> &GatewayReqPool;
        GatewayRequestQueue OutgoingRequests;

    public:
        GatewayServer(
            const int port, const int maxEvents, const int maxPending,
            StoragePool<ObjectBatch<Order, BatchSize>> &pool,
            OrderQueue *orders, ClientStates<MaxPositions> &states,
            VersionedFd &accountFd,
            SpscQueue<uint32_t, GatewayMaxClients> *outgoingDisconnects,
            StoragePool<RoutedAuthRequest> &gatewayReqPool,
            SpscQueue<RoutedAuthRequest *, GatewayMaxClients> *outgoingRequests)
            : Base(port, maxEvents, maxPending, pool, orders)
            , AccountFd(accountFd)
            , GatewayId(std::stoul(std::getenv("GATEWAY_ID") ?: "0"))
            , States(states)
            , OutgoingDisconnects(outgoingDisconnects)
            , GatewayReqPool(gatewayReqPool)
            , OutgoingRequests(outgoingRequests)
        {}

        void FirstMessageHandle(ClientAuthRequest *message,
                                uint32_t fd) noexcept
        {
            RoutedAuthRequest *finalRequest = GatewayReqPool.acquire();

            if (!finalRequest) [[unlikely]]
            {
                // Handle
                std::cout << "While handling first message: couldn't acquire "
                             "from pool.\n\n";
            }

            std::memcpy(finalRequest, message, sizeof(ClientAuthRequest));

            finalRequest->ClientFd = fd;
            finalRequest->GatewayId = GatewayId;

            bool enqueued = OutgoingRequests.TryPush(finalRequest);

            if (!enqueued) [[unlikely]]
            {
                std::cout << "Failed pushing the message to account "
                             "request sender.\n\n";

                // TODO : handle failure here
                if (!GatewayReqPool.localRelease(finalRequest)) [[unlikely]]
                {
                    // TODO : here too
                }
            }
            else
            {
                std::cout << "Successfully push to account request sender.\n\n";
            }
        }

        void BatchHandle(ObjectBatch<Order, BatchSize> *batch,
                         uint32_t fd) noexcept
        {
#ifdef NAOTO_PERF
            uint64_t now = now_tsc();
#endif

            batch->Fd = fd;
            batch->Auth = States.GetClientAuth(fd);

#ifdef NAOTO_PERF
            for (size_t i = 0; i < batch->Size; ++i)
            {
                (*batch)[i].IngestedTimestamp = now;
            }
#endif
        }

        void CloseHandle(uint32_t fd) noexcept
        {
            bool enqueued = OutgoingDisconnects.TryPush(fd);

            if (!enqueued) [[unlikely]]
            {
                std::cerr << "Epoll server: couldn't push the disconnect event "
                             "when handling a closed connection.\n\n";
            }
        }
    };
} // namespace naoto::order_gateway