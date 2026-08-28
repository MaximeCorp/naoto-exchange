#pragma once

#include <cstddef>

namespace naoto
{
    inline constexpr size_t MaxGateways = 16;
    inline constexpr size_t MaxClients = 1024;
    inline constexpr size_t MaxPositions = 16;
    inline constexpr size_t MaxAssets = 128;
    inline constexpr size_t MTU = 1500;
    inline constexpr size_t MaxTradeClient = 1000; // Expected max

    inline constexpr size_t MarketUpdateReceiveBufferSize = 1024;
    inline constexpr size_t MarketUpdateReceiveBatchSize = 16;
    inline constexpr size_t ClientStatesSwapBatchSize = 16;

    inline constexpr size_t MeEpollReceiveBatchSize = 16;
    inline constexpr size_t MeOrderStateQueueSize = 1024;
    inline constexpr size_t MeOrderBookUpdateQueueSize = 1024;
    inline constexpr size_t MeMaxOrderNodes = 16384;
    inline constexpr size_t MeMaxPriceLevels = 8192;
    inline constexpr size_t MeOrderStateBatchSize = 16;
    inline constexpr size_t MeOrderBookUpdateBatchSize = 16;

    inline constexpr size_t CdpEpollReceiveBatchSize = 16;
    inline constexpr size_t CdpResponsesResendBufferSize = 128;

    inline constexpr size_t GwMaxClients = 128;
    inline constexpr size_t GwEpollReceiveBatchSize = 16;
    inline constexpr size_t GwClientRequestResponseBatchSize = 16;
    inline constexpr size_t GwResendBufferSize = 32;
    inline constexpr size_t GwUpdateBufferSize = 1024;

    inline constexpr size_t CdpEpollCore = 12;
    inline constexpr size_t CdpKeeperCore = 12;
    inline constexpr size_t CdpSenderCore = 12;
    inline constexpr size_t CdpWriterCore = 12;
    inline constexpr size_t CdpMarketUpdateReceiveBufferSize = 1024;
    inline constexpr size_t CdpMarketUpdateReceiveBatchSize = 16;
    inline constexpr size_t CdpResponseBatchSize = 16;

} // namespace naoto
