#pragma once

#include <Consumer.hpp>
#include <OrderStateReport.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <UdpMulticastEmitter.hpp>

namespace MarketExecution
{
    class OrderStatusEmitter
        : public Consumer<MarketUpdatesEmitter, OrderStateReport>
        , public UdpMulticastEmitter<MarketUpdatesEmitter, OrderStateReport>
    {
    public:
        OrderStatusEmitter(const uint16_t portId, const uint16_t nbTxQueueSlots,
                           const uint16_t queueId, const unsigned lcoreId)
        {}

        void Handle(OrderStateReport *report) noexcept
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {
            }
        }
    };

    static int StartOrderStatusLoop(void *arg) noexcept
    {
        OrderStatusEmitter *emitter = static_cast<OrderStatusEmitter *>(arg);

        emitter.StartLoop();

        return 0;
    }
} // namespace MarketExecution
