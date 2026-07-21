#pragma once

#include <Consumer.hpp>
#include <OrderBookUpdate.hpp>
#include <UdpMulticastEmitter.hpp>

namespace MarketExecution
{
    class OrderBookEmitter
        : public Consumer<OrderBookEmitter, OrderBookUpdate>
        , public UdpMulticastEmitter<OrderBookEmitter, OrderBookUpdate>
    {
        void Handle(OrderBookUpdate *report) noexcept
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {
            }
        }
    };
};

static int StartOrderBookLoop(void *arg) noexcept
{
    OrderBookEmitter *emitter = static_cast<OrderBookEmitter *>(arg);

    emitter.StartLoop();

    return 0;
}
} // namespace MarketExecution