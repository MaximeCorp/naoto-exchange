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

    inline constexpr size_t TradeReportReceiveBufferSize = 1024;
    inline constexpr size_t TradeReportReceiveBatchSize = 16;
    inline constexpr size_t ClientStatesSwapBatchSize = 16;

    inline constexpr size_t MeEpollReceiveBatchSize = 16;
    inline constexpr size_t MeOrderStateQueueSize = 1024;
    inline constexpr size_t MeOrderBookUpdateQueueSize = 1024;
    inline constexpr size_t MeMaxOrderNodes = 16384;
    inline constexpr size_t MeMaxPriceLevels = 8192;
    inline constexpr size_t MeOrderStateBatchSize = 16;
    inline constexpr size_t MeOrderBookUpdateBatchSize = 16;

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
