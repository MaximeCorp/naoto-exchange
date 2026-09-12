#pragma once

#include <cstddef>
#include <matching_engine_types.hpp>
#include <order_book_update.hpp>
#include <system_conf.hpp>

namespace naoto::matching_engine
{
    class OrderBookEmitter
        : public OrderBookConsumerBase
        , public OrderBookEmitterBase
    {
    public:
        OrderBookEmitter(
            OrderBookUpdateQueue *incoming, OrderBookUpdateMempool &mempool,
            const uint16_t portId, const uint16_t nbTxQueueSlots,
            const uint16_t queueId, const unsigned lcoreId,
            const char *poolName, const size_t poolSize, const uint32_t srcIp,
            const uint32_t dstIp, const uint16_t srcPort,
            const uint16_t dstPort)
            : OrderBookConsumerBase(incoming, mempool)
            , OrderBookEmitterBase(portId, nbTxQueueSlots, queueId, lcoreId,
                                   poolName, poolSize, srcIp, dstIp, srcPort,
                                   dstPort)
        {}

        void Handle(OrderBookUpdate *report) noexcept
        {
            OrderBookEmitterBase::Send(report);
        }

        void Handle(OrderBookUpdateBatch &reports, size_t size) noexcept
        {
            OrderBookEmitterBase::Send(reports, size);
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                OrderBookConsumerBase::TryConsume();
            }
        }
    };

    inline int StartOrderBookLoop(void *arg) noexcept
    {
        OrderBookEmitter *emitter = static_cast<OrderBookEmitter *>(arg);

        emitter->StartLoop();

        return 0;
    }
} // namespace naoto::matching_engine
