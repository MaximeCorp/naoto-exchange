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
    inline constexpr size_t TradeReportReceiveQueueSize = 1024;
    inline constexpr size_t TradeReportReceivePoolSize = 16384;
    inline constexpr size_t ClientStatesSwapBatchSize = 16;

    inline constexpr size_t SharedMemoryQueueSize = 1024;
    inline constexpr char SharedMemoryPath[] = "/me_gw_shm";

    inline constexpr size_t MeEpollReceiveBatchSize = 16;
    inline constexpr size_t MeOrderQueueSize = 1024;
    inline constexpr size_t MeOrderStateQueueSize = 1024;
    inline constexpr size_t MeOrderBookUpdateQueueSize = 1024;
    inline constexpr size_t MeMaxPriceLevels = 8192;
    inline constexpr size_t MeOrderStatePoolSize = 1024;
    inline constexpr size_t MeOrderBookUpdatePoolSize = 1024;
    inline constexpr size_t MeOrderStateBatchSize = 16;
    inline constexpr size_t MeOrderBookUpdateBatchSize = 16;
    inline constexpr size_t MeOrderPoolSize = 1024;
    inline constexpr size_t MeOrderNodePoolSize = 1024;
    inline constexpr size_t MePriceLevelPoolSize = 1024;
    inline constexpr size_t MeSkipListNodePoolSize = 1024;

    inline constexpr size_t MeOrdersPerClientFactor = 16;

    inline constexpr size_t MeMaxOrderNodes =
        MaxTradeClient * MeOrdersPerClientFactor;
    inline constexpr size_t MeOrderMapSize =
        std::bit_ceil((MeMaxOrderNodes * 2) / 16);
    inline constexpr size_t MeFastMapSize =
        std::bit_ceil((MeMaxPriceLevels * 2) / 16);

    inline constexpr size_t MeSkipListMaxLevel =
        std::bit_width(MeMaxPriceLevels);

    inline constexpr size_t GatewayMaxClients = 128;
    inline constexpr size_t GatewayEpollReceiveBatchSize = 16;
    inline constexpr size_t GatewayEpollReceiveQueueSize = 16384;
    inline constexpr size_t GatewayClientRequestResponseQueueSize = 1024;
    inline constexpr size_t GatewayClientRequestResponsePoolSize = 1024;
    inline constexpr size_t GatewayClientRequestResponseBatchSize = 16;
    inline constexpr size_t GatewayResendBufferSize = 32;
    inline constexpr size_t GatewayUpdateBufferSize = 1024;
    inline constexpr size_t GatewayRequestPoolSize = 128;
    inline constexpr size_t GatewayRequestQueueSize = 128;
    inline constexpr size_t GatewayOrderConfirmationQueueSize = 16384;
    inline constexpr size_t GatewayOrderPoolSize = 16384;

    inline constexpr size_t AccountEpollCore = 12;
    inline constexpr size_t AccountEpollReceiveQueueSize = 1024;
    inline constexpr size_t AccountEpollReceiveBatchSize = 16;
    inline constexpr size_t AccountRequestPoolSize = 128;
    inline constexpr size_t AccountResponsesResendBufferSize = 128;
    inline constexpr size_t AccountProcessorCore = 12;
    inline constexpr size_t AccountSenderCore = 12;
    inline constexpr size_t AccountWriterCore = 12;
    inline constexpr size_t AccountResponseBatchSize = 16;
    inline constexpr size_t AccountResponsePoolSize = 128;
} // namespace naoto
