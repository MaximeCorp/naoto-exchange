#pragma once

#include <Consumer.hpp>
#include <FdGen.hpp>
#include <GatewayRequest.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <sys/socket.h>

namespace Gateways
{
    class ConsumerImpl : public Consumer<ConsumerImpl, GatewayRequest>
    {
        using Base = Consumer<ConsumerImpl, GatewayRequest>;
        using RequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<GatewayRequest *>;

    private:
        FdGen &CdpFd;

        void SendRequest(GatewayRequest *curRequest) noexcept
        {
            std::cout << "Sending request to CDP\n\n";
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
        ConsumerImpl(RequestQueue &requestsQueue,
                     StoragePool<GatewayRequest> &requestsPool, FdGen &cdpFd)
            : Base(requestsQueue, requestsPool)
            , CdpFd(cdpFd)
        {}

        void Handle(GatewayRequest *curRequest) noexcept
        {
            std::cout << "Cdp request sender received the request and will try "
                         "to send it.\n\n";
            SendRequest(curRequest);
        }
    };

    class CdpRequestSender
    {
        using RequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<GatewayRequest *>;

    private:
        ConsumerImpl EpollConsumer;
        ConsumerImpl StatesWriterConsumer;

    public:
        CdpRequestSender(RequestQueue &epollRequests,
                         RequestQueue &statesWriterRequests,
                         StoragePool<GatewayRequest> &epollPool,
                         StoragePool<GatewayRequest> &statesWriterPool,
                         FdGen &cdpFd)
            : EpollConsumer(epollRequests, epollPool, cdpFd)
            , StatesWriterConsumer(statesWriterRequests, statesWriterPool,
                                   cdpFd)
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {
                EpollConsumer.ConsumeTimed(75);
                StatesWriterConsumer.ConsumeTimed(75);
            }
        }
    };

} // namespace Gateways