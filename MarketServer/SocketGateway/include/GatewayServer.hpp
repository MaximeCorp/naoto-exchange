#pragma once

#include <ClientRequest.hpp>
#include <EpollServer.hpp>
#include <FdGen.hpp>
#include <GatewayRequest.hpp>
#include <atomic>
#include <string>

namespace Gateways
{
    template <size_t BatchSize>
    class Gateway : public EpollServer<Gateway, Order, BatchSize, ClientRequest>
    {
        using Base = EpollServer < Gateway, Order, BatchSize, ClientRequest;

    private:
        FdGen &CdpFd;
        uint16_t GatewayId;

        void SendRequest(GatewayRequest *curRequest) noexcept
        {
            uint64_t curVal =
                CdpFd.load(std::memory_order_relaxed); // Relaxed because
                                                       // memory dependancy
                                                       // allows it

            int32_t curFd = FdGen::Fd(curVal);

            if (curFd == -1) [[unlikely]]
            {
                std::cerr << "Failed fetching user details from cdp: not "
                             "connected\n\n";
                return;
            }

            // edge case: if fd gets closed then recycled by and the
            // new fd is for client, then information leak, handle
            // this by closing fd after making sure the sender has
            // seen the new fd

            ssize_t sent =
                send(curFd, curRequest, sizeof(GatewayRequest), MSG_NOSIGNAL);

            std::cerr << "Sending request to fd " << curFd
                      << ", size=" << sizeof(GatewayRequest)
                      << ", sent=" << sent << "\n";

            // TODO: modify the logic here to call send as many times as
            // necessary, apply same modifications to risk check sendOrder

            if (sent < 0) [[unlikely]]
            {
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    // handle order rejection
                }
                // reject order
                return;
            }
            // edge case: send < sizeof(Order)

            uint64_t newVal = CdpFd.load(std::memory_order_acquire);

            if (newVal != curVal) [[unlikely]]
            {
                // Means the send was potentially sent to the wrong
                // fd
                // For later : push to the array / vector of
                // messages to send again
            }
        }

    public:
        EpollServer(const int port, const int maxEvents, const int maxPending,
                    StoragePool<ObjectBatch<T, BatchSize>> &pool,
                    ObjectQueue &orders, FdGen &cdpFd)
            : Base(port, maxEvents, maxPending, pool, orders)
            , CdpFd(cdpFd)
            , GatewayId(std::stoul(std::getenv("GATEWAY_ID") ?: "0"))
        {}

        void FirstMessageHandle(ClientRequest *message, uint32_t fd) noexcept
        {
            GatewayRequest finalRequest;

            std::memcpy(&finalRequest, curRequest, sizeof(ClientRequest));

            finalRequest.ClientFd = fd;
            finalRequest.GatewayId = GatewayId;

            sendRequest(&finalRequest);
        }
    };
} // namespace Gateways