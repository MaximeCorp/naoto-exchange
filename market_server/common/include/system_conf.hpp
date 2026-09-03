#pragma once

#include <bit>
#include <concepts.hpp>
#include <cstddef>

namespace naoto
{
    inline constexpr size_t MaxGateways = 16;
    inline constexpr size_t MaxClients = 1024;
    inline constexpr size_t MaxPositions = 16;
    inline constexpr size_t MaxAssets = 128;
    inline constexpr size_t MTU = 1500;
    inline constexpr size_t MaxTradeClient = 1000; // Expected max

    inline constexpr size_t TradeReportReceiveBufferSize = 1024;
    inline constexpr size_t TradeReportReceiveBatchSize = 16;
    inline constexpr size_t ClientStatesSwapBatchSize = 16;

    inline constexpr size_t MeEpollReceiveBatchSize = 16;
    inline constexpr size_t MeOrderStateQueueSize = 1024;
    inline constexpr size_t MeOrderBookUpdateQueueSize = 1024;
    inline constexpr size_t MeMaxPriceLevels = 8192;
    inline constexpr size_t MeOrderStateBatchSize = 16;
    inline constexpr size_t MeOrderBookUpdateBatchSize = 16;

    // --- Derived sizing --------------------------------------------
    // Values below are a *function* of the knobs above rather than
    // independent magic numbers, so scaling MaxTradeClient/
    // MeMaxPriceLevels automatically scales everything that actually
    // depends on them, instead of requiring every downstream constant
    // to be bumped by hand and risking drift (see
    // MeMaxOrderNodes/MeOrderMapSize/MeFastMapSize/MeSkipListMaxLevel
    // below, all previously hardcoded independently of each other and
    // of MaxTradeClient/MeMaxPriceLevels in matching_engine/src/main.cpp).

    // How many orders a single client can plausibly have resting across
    // the whole book at once. This is capacity headroom, not a hard
    // per-client cap - a client that bursts past it just means the pool
    // is shared a little more tightly that tick, not that the order is
    // rejected.
    inline constexpr size_t MeOrdersPerClientFactor = 16;

    // Total resting-order capacity across the whole matching engine -
    // backs OrderNodePool (a plain SingleThreadedStoragePool, no
    // power-of-two requirement).
    inline constexpr size_t MeMaxOrderNodes =
        MaxTradeClient * MeOrdersPerClientFactor;

    // FlatHashMap's internal layout is Size buckets * 16 slots/bucket
    // with bitmask indexing (see PowerOfTwo, concepts.hpp - Size must
    // be a power of two, enforced on FlatHashMap itself). Size is
    // picked so the map holds its target item count at roughly 50%
    // load factor: Robin Hood hashing degrades hard as load factor
    // approaches 100% (inserts on a full/near-full table can silently
    // fail - see FlatHashMapTest.AddNodeOnFullTableSilentlyDropsWithoutCorrupting
    // in test_flat_hash_map.cpp), so this leaves real headroom rather
    // than sizing exactly to the expected count.
    inline constexpr size_t MeOrderMapSize =
        std::bit_ceil((MeMaxOrderNodes * 2) / 16);
    inline constexpr size_t MeFastMapSize =
        std::bit_ceil((MeMaxPriceLevels * 2) / 16);

    // Skip list depth: the standard heuristic for a skip list expected
    // to hold N nodes is ~log2(N) levels - this was previously a
    // hand-picked 10, which implicitly assumed ~1024 price levels
    // rather than the 8192 MeMaxPriceLevels actually configured below.
    inline constexpr size_t MeSkipListMaxLevel = std::bit_width(MeMaxPriceLevels);

    inline constexpr size_t AccountEpollReceiveBatchSize = 16;
    inline constexpr size_t AccountResponsesResendBufferSize = 128;

    inline constexpr size_t GatewayMaxClients = 128;
    inline constexpr size_t GatewayEpollReceiveBatchSize = 16;
    inline constexpr size_t GatewayClientRequestResponseBatchSize = 16;
    inline constexpr size_t GatewayResendBufferSize = 32;
    inline constexpr size_t GatewayUpdateBufferSize = 1024;

    inline constexpr size_t AccountEpollCore = 12;
    inline constexpr size_t AccountProcessorCore = 12;
    inline constexpr size_t AccountSenderCore = 12;
    inline constexpr size_t AccountWriterCore = 12;
    inline constexpr size_t AccountTradeReportReceiveBufferSize = 1024;
    inline constexpr size_t AccountTradeReportReceiveBatchSize = 16;
    inline constexpr size_t AccountResponseBatchSize = 16;

} // namespace naoto
