#pragma once

#include <consumer.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>
#include <udp_multicast_emitter.hpp>

namespace naoto::matching_engine
{
    template <size_t BatchSize = 0>
    class OrderStatusEmitter
        : public Consumer<OrderStatusEmitter<BatchSize>, OrderStateReport,
                          BatchSize>
        , public UdpMulticastEmitter<OrderStatusEmitter<BatchSize>,
                                     OrderStateReport, BatchSize>
    {
        using UpdatesQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;

    public:
        OrderStatusEmitter(UpdatesQueue &incoming,
                           StoragePool<OrderStateReport> &mempool,
                           const uint16_t portId, const uint16_t nbTxQueueSlots,
                           const uint16_t queueId, const unsigned lcoreId,
                           const char *poolName, const size_t poolSize,
                           const uint32_t srcIp, const uint32_t dstIp,
                           const uint16_t srcPort, const uint16_t dstPort)
            : Consumer<OrderStatusEmitter<BatchSize>, OrderStateReport,
                       BatchSize>(incoming, mempool)
            , UdpMulticastEmitter<OrderStatusEmitter<BatchSize>,
                                  OrderStateReport, BatchSize>(
                  portId, nbTxQueueSlots, queueId, lcoreId, poolName, poolSize,
                  srcIp, dstIp, srcPort, dstPort)
        {}

        void Handle(OrderStateReport *report) noexcept
        {
            UdpMulticastEmitter<OrderStatusEmitter, OrderStateReport,
                                BatchSize>::Send(report);
        }

        void Handle(std::array<OrderStateReport *, BatchSize> &reports,
                    size_t size) noexcept
            requires(BatchSize > 0)
        {
            std::cout << "Order status about to be sent\n\n";
            UdpMulticastEmitter<OrderStatusEmitter, OrderStateReport,
                                BatchSize>::Send(reports, size);
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                Consumer<OrderStatusEmitter<BatchSize>, OrderStateReport,
                         BatchSize>::TryConsume();
            }
        }
    };

    template <size_t BatchSize = 0>
    static int StartOrderStatusLoop(void *arg) noexcept
    {
        std::cout << "Starting the order emitting loop\n";
        OrderStatusEmitter<BatchSize> *emitter =
            static_cast<OrderStatusEmitter<BatchSize> *>(arg);

        emitter->StartLoop();

        return 0;
    }
} // namespace naoto::matching_engine
