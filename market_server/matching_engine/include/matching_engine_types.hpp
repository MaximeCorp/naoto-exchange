#pragma once

#include <array>
#include <consumer.hpp>
#include <cstdint>
#include <epoll_server.hpp>
#include <flat_hash_map.hpp>
#include <functional>
#include <object_batch.hpp>
#include <order.hpp>
#include <order_book_update.hpp>
#include <order_node.hpp>
#include <order_state_report.hpp>
#include <price_level.hpp>
#include <single_threaded_storage_pool.hpp>
#include <skip_list.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <udp_multicast_emitter.hpp>

// Every concrete type the matching engine is built out of, in one place.
// Changing a queue depth, a batch size or the object a pool holds is a
// one-line edit here (or in system_conf.hpp) instead of a hunt through
// every class that names the instantiation.
namespace naoto::matching_engine
{
    // ---------------------------------------------------------- orders in
    using OrderBatch = ObjectBatch<Order, MeEpollReceiveBatchSize>;
    using OrderBatchQueue = SpscQueue<OrderBatch *, MeOrderQueueSize>;
    using OrderBatchConsumer = SpscQueueConsumer<OrderBatch *, MeOrderQueueSize>;
    using OrderBatchMempool = StoragePool<OrderBatch, MeOrderPoolSize>;

    // ------------------------------------------------- order states out
    using OrderStateQueue =
        SpscQueue<OrderStateReport *, MeOrderStateQueueSize>;
    using OrderStateProducer =
        SpscQueueProducer<OrderStateReport *, MeOrderStateQueueSize>;
    using OrderStateMempool =
        StoragePool<OrderStateReport, MeOrderStatePoolSize>;
    using OrderStateBatch =
        std::array<OrderStateReport *, MeOrderStateBatchSize>;

    // --------------------------------------------- order book updates out
    using OrderBookUpdateQueue =
        SpscQueue<OrderBookUpdate *, MeOrderBookUpdateQueueSize>;
    using OrderBookUpdateProducer =
        SpscQueueProducer<OrderBookUpdate *, MeOrderBookUpdateQueueSize>;
    using OrderBookUpdateMempool =
        StoragePool<OrderBookUpdate, MeOrderBookUpdatePoolSize>;
    using OrderBookUpdateBatch =
        std::array<OrderBookUpdate *, MeOrderBookUpdateBatchSize>;

    // ------------------------------------------------------- book storage
    using OrderNodeMempool =
        SingleThreadedStoragePool<OrderNode, MeOrderNodePoolSize>;
    using PriceLevelMempool =
        SingleThreadedStoragePool<PriceLevel, MePriceLevelPoolSize>;

    // order id -> resting order; price -> level, for O(1) lookup
    using OrderIdMap = FlatHashMap<uint64_t, OrderNode *, MeOrderMapSize>;
    using PriceLevelMap = FlatHashMap<int64_t, PriceLevel *, MeFastMapSize>;

    // Ordered price levels. Compare is std::less for the ask side and
    // std::greater for the bid side, so it stays a parameter.
    template <typename Compare = std::less<std::int64_t>>
    using PriceLevelSkipList = SkipList<int64_t, PriceLevel *,
                                        MeSkipListNodePoolSize,
                                        MeSkipListMaxLevel, Compare>;

    // ---------------------------------------------------- CRTP base types
    // Forward declarations: the bases below are parameterised on the
    // derived class, which is only completed in its own header.
    class OrderIngressServer;
    class TradeReportEmitter;
    class OrderBookEmitter;

    using OrderIngressServerBase =
        EpollServer<OrderIngressServer, Order, MeEpollReceiveBatchSize,
                    MeOrderQueueSize, MeOrderPoolSize>;

    using TradeReportConsumerBase =
        Consumer<TradeReportEmitter, OrderStateReport, MeOrderStateQueueSize,
                 MeOrderStatePoolSize, MeOrderStateBatchSize>;
    using TradeReportEmitterBase =
        UdpMulticastEmitter<TradeReportEmitter, OrderStateReport,
                            MeOrderStateBatchSize>;

    using OrderBookConsumerBase =
        Consumer<OrderBookEmitter, OrderBookUpdate, MeOrderBookUpdateQueueSize,
                 MeOrderBookUpdatePoolSize, MeOrderBookUpdateBatchSize>;
    using OrderBookEmitterBase =
        UdpMulticastEmitter<OrderBookEmitter, OrderBookUpdate,
                            MeOrderBookUpdateBatchSize>;
} // namespace naoto::matching_engine
