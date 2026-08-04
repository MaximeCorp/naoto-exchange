#pragma once

#include <EpollServer.hpp>
#include <Order.hpp>

namespace MarketExecution
{
    template <size_t BatchSize>
    class OrderIngressServer
        : public EpollServer<OrderIngressServer<BatchSize>, Order, BatchSize>
    {
        using Base =
            EpollServer<OrderIngressServer<BatchSize>, Order, BatchSize>;

    public:
        using Base::Base;
    };
} // namespace MarketExecution