#pragma once

#include <atomic>
#include <client_auth_request.hpp>
#include <client_states.hpp>
#include <cstdlib>
#include <cstring>
#include <order_gateway_types.hpp>
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
    class GatewayServer : public GatewayServerBase
    {
    private:
        VersionedFd &AccountFd;
        uint16_t GatewayId;
        ClientStates &States;
        DisconnectProducer OutgoingDisconnects;
        AuthRequestMempool &GatewayReqPool;
        AuthRequestProducer OutgoingRequests;

    public:
        GatewayServer(
            const int port, const int maxEvents, const int maxPending,
            OrderBatchMempool &pool, OrderBatchQueue *orders,
            ClientStates &states, VersionedFd &accountFd,
            DisconnectQueue *outgoingDisconnects,
            AuthRequestMempool &gatewayReqPool,
            AuthRequestQueue *outgoingRequests)
            : GatewayServerBase(port, maxEvents, maxPending, pool, orders)
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
            RoutedAuthRequest *finalRequest = GatewayReqPool.Acquire();

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
                if (!GatewayReqPool.LocalRelease(finalRequest)) [[unlikely]]
                {
                    // TODO : here too
                }
            }
            else
            {
                std::cout << "Successfully push to account request sender.\n\n";
            }
        }

        void BatchHandle(OrderBatch *batch,
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