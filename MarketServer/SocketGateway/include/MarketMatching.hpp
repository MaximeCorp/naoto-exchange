#pragma once

#include <OrderBatch.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstdint>
#include <errno.h>
#include <iostream>
#include <sys/socket.h>
#include <vector>

namespace Gateways
{
    enum SendStatus
    {
        SENT,
        RETRY,
        CLOSED,
        ERROR
    };

    template <size_t BatchSize>
    class MarketMatching
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        // The ip list could be given with config file
        std::vector<std::uint32_t>
            MatchingEngines; // Mapping from asset ID to matching engine fd

        void connectMatchingEngines(void) // Might need some args
        {
            // Todo
        }

        void processBatch(OrderBatch<BatchSize> *batch)
        {
            for (size_t i = 0; i < batch->getSize(); ++i)
            {
                Order &curOrder = batch[i];

                if (curOrder.getStatus() == OrderStatus::ACCEPTED) [[likely]]
                {
                    ssize_t bytes_sent =
                        send(MatchingEngines[curOrder.getAssett()], &curOrder,
                             sizeof(Order), MSG_DONTWAIT | MSG_NOSIGNAL);
                    if (bytes_sent == -1)
                    {
                        SendStatus status = SendStatus::RETRY;

                        while (status != SendStatus::SENT)
                        {
                            switch (errno)
                            {
                            case EAGAIN:
                            case EWOULDBLOCK:
                                // The kernel's TCP send buffer is full.
                                // In HFT, we usually "spin" (busy-wait) or
                                // push to a local retry queue.
                                return SendStatus::RETRY;

                            case EPIPE:
                            case ECONNRESET:
                            case ENOTCONN:
                                // The Matching Engine has disconnected or
                                // crashed.
                                std::cerr << "Matching Engine connection lost!"
                                          << std::endl;
                                return SendStatus::CLOSED;

                            case EINTR:
                                return SendStatus::RETRY;

                            default:
                                // Something is fundamentally wrong (e.g., EBADF
                                // or EFAULT).
                                std::perror("Fatal send error");
                                return SendStatus::ERROR;
                            }

                            if (status == SendStatus::RETRY) [[likely]]
                            {
                                bytes_sent =
                                    send(MatchingEngines[curOrder.getAssett()],
                                         &curOrder, sizeof(Order),
                                         MSG_DONTWAIT | MSG_NOSIGNAL);
                            }
                        }
                    }
                }
            }
        }

    public:
        MarketMatching();
        ~MarketMatching();
    };
} // namespace Gateways
