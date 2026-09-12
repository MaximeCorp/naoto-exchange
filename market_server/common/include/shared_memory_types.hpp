#pragma once

#include <order.hpp>
#include <spsc_queue.hpp>
#include <system_conf.hpp>

namespace naoto
{
    using OrderQueue = SpscQueue<Order, SharedMemoryQueueSize>;
    using OrderProducer = SpscQueueProducer<Order, SharedMemoryQueueSize>;
    using OrderConsumer = SpscQueueConsumer<Order, SharedMemoryQueueSize>;
} // namespace naoto