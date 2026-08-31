#pragma once

#include <atomic>
#include <client_auth_request.hpp>
#include <client_states.hpp>
#include <epoll_server.hpp>
#include <versioned_fd.hpp>
#include <routed_auth_request.hpp>
#include <readerwritercircularbuffer.h>
#include <string>

namespace naoto::order_gateway
{
    template <size_t BatchSize, size_t MaxPositions>
    class GatewayServer
        : public EpollServer<GatewayServer<BatchSize, MaxPositions>, Order,
                             BatchSize, ClientAuthRequest>
    {
        using Base = EpollServer<GatewayServer<BatchSize, MaxPositions>, Order,
                                 BatchSize, ClientAuthRequest>;
        using DisconnectQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<uint32_t>;
        using GatewayRequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<RoutedAuthRequest *>;
        using OrderQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;

    private:
        VersionedFd &AccountFd;
        uint16_t GatewayId;
        ClientStates<MaxPositions> &States;
        DisconnectQueue &OutgoingDisconnects;
        StoragePool<RoutedAuthRequest> &GatewayReqPool;
        GatewayRequestQueue &OutgoingRequests;

    public:
        GatewayServer(const int port, const int maxEvents, const int maxPending,
                      StoragePool<ObjectBatch<Order, BatchSize>> &pool,
                      OrderQueue &orders, ClientStates<MaxPositions> &states,
                      VersionedFd &accountFd,
                      DisconnectQueue &outgoingDisconnects,
                      StoragePool<RoutedAuthRequest> &gatewayReqPool,
                      GatewayRequestQueue &outgoingRequests)
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

            bool enqueued = OutgoingRequests.try_enqueue(finalRequest);

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
            std::cout << "Checking auth from gateway\nClient state:\n\n";

            States.GetClientState(fd).log();

            batch->Fd = fd;
            batch->Auth = States.GetClientAuth(fd);
        }

        void CloseHandle(uint32_t fd) noexcept
        {
            bool enqueued = OutgoingDisconnects.try_enqueue(fd);

            if (!enqueued) [[unlikely]]
            {
                std::cerr << "Epoll server: couldn't push the disconnect event "
                             "when handling a closed connection.\n\n";
            }
        }
    };
} // namespace naoto::order_gateway