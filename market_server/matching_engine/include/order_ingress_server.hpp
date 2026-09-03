#pragma once

#include <epoll_server.hpp>
#include <order.hpp>
#include <timestamps.hpp>

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

        void BatchHandle(ObjectBatch<Order, BatchSize> *batch,
                         uint32_t fd) noexcept
        {
            uint64_t now = now_tsc();

            for (size_t i = 0; i < batch->Size; ++i)
            {
                batch[i].IngestedTimestamp = now;
            }
        }
    };
} // namespace naoto::matching_engine