#pragma once

#include <Consumer.hpp>
#include <OrderStateReport.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <UdpMulticastEmitter.hpp>

namespace MarketeExecution
{
    class MarketUpdatesEmitter
        : public Consumer<MarketUpdatesEmitter, OrderStateReport>
        , public UdpMulticastEmitter<MarketUpdatesEmitter, OrderStateReport>
    {
        void Handle(OrderStateReport *report)
        {}
    };
} // namespace MarketeExecution
