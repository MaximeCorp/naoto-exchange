#pragma once

#include <cstddef>
#include <iostream>
#include <matching_engine_types.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <system_conf.hpp>

namespace naoto::matching_engine
{
    class TradeReportEmitter
        : public TradeReportConsumerBase
        , public TradeReportEmitterBase
    {
    public:
        TradeReportEmitter(
            OrderStateQueue *incoming, OrderStateMempool &mempool,
            const uint16_t portId, const uint16_t nbTxQueueSlots,
            const uint16_t queueId, const unsigned lcoreId,
            const char *poolName, const size_t poolSize, const uint32_t srcIp,
            const uint32_t dstIp, const uint16_t srcPort,
            const uint16_t dstPort)
            : TradeReportConsumerBase(incoming, mempool)
            , TradeReportEmitterBase(portId, nbTxQueueSlots, queueId, lcoreId,
                                     poolName, poolSize, srcIp, dstIp, srcPort,
                                     dstPort)
        {}

        void Handle(OrderStateReport *report) noexcept
        {
            TradeReportEmitterBase::Send(report);
        }

        void Handle(OrderStateBatch &reports, size_t size) noexcept
        {
            std::cout << "Order status about to be sent\n\n";
            TradeReportEmitterBase::Send(reports, size);
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                TradeReportConsumerBase::TryConsume();
            }
        }
    };

    inline int StartTradeReportLoop(void *arg) noexcept
    {
        std::cout << "Starting the order emitting loop\n";
        TradeReportEmitter *emitter = static_cast<TradeReportEmitter *>(arg);

        emitter->StartLoop();

        return 0;
    }
} // namespace naoto::matching_engine
