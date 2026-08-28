#pragma once

#include <atomic>
#include <client_request.hpp>
#include <client_states.hpp>
#include <epoll_server.hpp>
#include <fd_gen.hpp>
#include <gateway_request.hpp>
#include <readerwritercircularbuffer.h>
#include <string>

namespace naoto::order_gateway
{
    template <size_t BatchSize, size_t MaxPositions>
    class GatewayServer
        : public EpollServer<GatewayServer<BatchSize, MaxPositions>, Order,
                             BatchSize, ClientRequest>
    {
        using Base = EpollServer<GatewayServer<BatchSize, MaxPositions>, Order,
                                 BatchSize, ClientRequest>;
        using DisconnectQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<uint32_t>;
        using GatewayRequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<GatewayRequest *>;
        using OrderQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;

    private:
        FdGen &CdpFd;
        uint16_t GatewayId;
        ClientStates<MaxPositions> &States;
        DisconnectQueue &OutgoingDisconnects;
        StoragePool<GatewayRequest> &GwReqPool;
        GatewayRequestQueue &OutgoingRequests;

    public:
        GatewayServer(const int port, const int maxEvents, const int maxPending,
                      StoragePool<ObjectBatch<Order, BatchSize>> &pool,
                      OrderQueue &orders, ClientStates<MaxPositions> &states,
                      FdGen &cdpFd, DisconnectQueue &outgoingDisconnects,
                      StoragePool<GatewayRequest> &gwReqPool,
                      GatewayRequestQueue &outgoingRequests)
            : Base(port, maxEvents, maxPending, pool, orders)
            , CdpFd(cdpFd)
            , GatewayId(std::stoul(std::getenv("GATEWAY_ID") ?: "0"))
            , States(states)
            , OutgoingDisconnects(outgoingDisconnects)
            , GwReqPool(gwReqPool)
            , OutgoingRequests(outgoingRequests)
        {}

        void FirstMessageHandle(ClientRequest *message, uint32_t fd) noexcept
        {
            GatewayRequest *finalRequest = GwReqPool.acquire();

            if (!finalRequest) [[unlikely]]
            {
                // Handle
                std::cout << "While handling first message: couldn't acquire "
                             "from pool.\n\n";
            }

            std::memcpy(finalRequest, message, sizeof(ClientRequest));

            finalRequest->ClientFd = fd;
            finalRequest->GatewayId = GatewayId;

            bool enqueued = OutgoingRequests.try_enqueue(finalRequest);

            if (!enqueued) [[unlikely]]
            {
                std::cout
                    << "Failed pushing the message to cdp request sender.\n\n";

                // TODO : handle failure here
                if (!GwReqPool.localRelease(finalRequest)) [[unlikely]]
                {
                    // TODO : here too
                }
            }
            else
            {
                std::cout << "Successfully push to cdp request sender.\n\n";
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