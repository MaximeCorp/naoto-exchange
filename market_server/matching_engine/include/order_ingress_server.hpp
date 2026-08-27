#pragma once

#include <epoll_server.hpp>
#include <order.hpp>

namespace naoto::matching_engine
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
} // namespace naoto::matching_engine