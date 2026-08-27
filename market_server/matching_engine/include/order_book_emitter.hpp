#pragma once

#include <array>
#include <consumer.hpp>
#include <cstddef>
#include <order_book_update.hpp>
#include <udp_multicast_emitter.hpp>

namespace naoto::matching_engine
{
    template <size_t BatchSize = 0>
    class OrderBookEmitter
        : public Consumer<OrderBookEmitter<BatchSize>, OrderBookUpdate,
                          BatchSize>
        , public UdpMulticastEmitter<OrderBookEmitter<BatchSize>,
                                     OrderBookUpdate, BatchSize>
    {
        using UpdatesQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderBookUpdate *>;
        using EmitterBase = UdpMulticastEmitter<OrderBookEmitter<BatchSize>,
                                                OrderBookUpdate, BatchSize>;

    public:
        OrderBookEmitter(UpdatesQueue &incoming,
                         StoragePool<OrderBookUpdate> &mempool,
                         const uint16_t portId, const uint16_t nbTxQueueSlots,
                         const uint16_t queueId, const unsigned lcoreId,
                         const char *poolName, const size_t poolSize,
                         const uint32_t srcIp, const uint32_t dstIp,
                         const uint16_t srcPort, const uint16_t dstPort)
            : Consumer<OrderBookEmitter<BatchSize>, OrderBookUpdate, BatchSize>(
                  incoming, mempool)
            , UdpMulticastEmitter<OrderBookEmitter<BatchSize>, OrderBookUpdate,
                                  BatchSize>(portId, nbTxQueueSlots, queueId,
                                             lcoreId, poolName, poolSize, srcIp,
                                             dstIp, srcPort, dstPort)
        {}

        void Handle(OrderBookUpdate *report) noexcept
        {
            EmitterBase::Send(report);
        }

        void Handle(std::array<OrderBookUpdate *, BatchSize> &reports,
                    size_t size) noexcept
            requires(BatchSize > 0)
        {
            EmitterBase::Send(reports, size);
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                Consumer<OrderBookEmitter, OrderBookUpdate,
                         BatchSize>::TryConsume();
            }
        }
    };

    template <size_t BatchSize = 0>
    static int StartOrderBookLoop(void *arg) noexcept
    {
        OrderBookEmitter<BatchSize> *emitter =
            static_cast<OrderBookEmitter<BatchSize> *>(arg);

        emitter->StartLoop();

        return 0;
    }
} // namespace naoto::matching_engine