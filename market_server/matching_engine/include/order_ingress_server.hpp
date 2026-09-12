#pragma once

#include <matching_engine_types.hpp>
#include <order.hpp>
#include <system_conf.hpp>

#ifdef NAOTO_PERF
#    include <timestamps.hpp>
#endif

namespace naoto::matching_engine
{
    class OrderIngressServer : public OrderIngressServerBase
    {
    public:
        using OrderIngressServerBase::OrderIngressServerBase;

#ifdef NAOTO_PERF
        void BatchHandle(OrderBatch *batch,
                         uint32_t fd) noexcept
        {
            batch->Fd = fd;

            uint64_t now = now_tsc();

            for (size_t i = 0; i < batch->Size; ++i)
            {
                (*batch)[i].ReceivedTimestamp = now;
            }
        }
#endif
    };
} // namespace naoto::matching_engine
