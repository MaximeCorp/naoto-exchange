#pragma once

#include <consumer.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <udp_multicast_emitter.hpp>

namespace naoto::matching_engine
{
    template <size_t BatchSize = 0>
    class TradeReportEmitter
        : public Consumer<TradeReportEmitter<BatchSize>, OrderStateReport,
                          MeOrderStateQueueSize, MeOrderStatePoolSize,
                          BatchSize>
        , public UdpMulticastEmitter<TradeReportEmitter<BatchSize>,
                                     OrderStateReport, BatchSize>
    {
        using UpdatesQueue =
            SpscQueue<OrderStateReport *, MeOrderStateQueueSize>;

    public:
        TradeReportEmitter(UpdatesQueue *incoming,
                           StoragePool<OrderStateReport> &mempool,
                           const uint16_t portId, const uint16_t nbTxQueueSlots,
                           const uint16_t queueId, const unsigned lcoreId,
                           const char *poolName, const size_t poolSize,
                           const uint32_t srcIp, const uint32_t dstIp,
                           const uint16_t srcPort, const uint16_t dstPort)
            : Consumer<TradeReportEmitter<BatchSize>, OrderStateReport,
                       BatchSize>(incoming, mempool)
            , UdpMulticastEmitter<TradeReportEmitter<BatchSize>,
                                  OrderStateReport, BatchSize>(
                  portId, nbTxQueueSlots, queueId, lcoreId, poolName, poolSize,
                  srcIp, dstIp, srcPort, dstPort)
        {}

        void Handle(OrderStateReport *report) noexcept
        {
            UdpMulticastEmitter<TradeReportEmitter, OrderStateReport,
                                BatchSize>::Send(report);
        }

        void Handle(std::array<OrderStateReport *, BatchSize> &reports,
                    size_t size) noexcept
            requires(BatchSize > 0)
        {
            std::cout << "Order status about to be sent\n\n";
            UdpMulticastEmitter<TradeReportEmitter, OrderStateReport,
                                BatchSize>::Send(reports, size);
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                Consumer<TradeReportEmitter<BatchSize>, OrderStateReport,
                         BatchSize>::TryConsume();
            }
        }
    };

    template <size_t BatchSize = 0>
    static int StartTradeReportLoop(void *arg) noexcept
    {
        std::cout << "Starting the order emitting loop\n";
        TradeReportEmitter<BatchSize> *emitter =
            static_cast<TradeReportEmitter<BatchSize> *>(arg);

        emitter->StartLoop();

        return 0;
    }
} // namespace naoto::matching_engine
