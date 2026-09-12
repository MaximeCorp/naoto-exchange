#pragma once

#include <array>
#include <bytes_buffer.hpp>
#include <client_account_snapshot.hpp>
#include <client_auth_request.hpp>
#include <client_message.hpp>
#include <consumer.hpp>
#include <cstdint>
#include <epoll_server.hpp>
#include <flat_hash_map.hpp>
#include <object_batch.hpp>
#include <order.hpp>
#include <order_confirmation.hpp>
#include <order_state_report.hpp>
#include <routed_auth_request.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <udp_multicast_receiver.hpp>

// Every concrete type the order gateway is built out of, in one place.
// Changing a queue depth, a batch size or the object a pool holds is a
// one-line edit here (or in system_conf.hpp) instead of a hunt through
// every class that names the instantiation.
namespace naoto::order_gateway
{
    // ------------------------------------------------- orders from clients
    using OrderBatch = ObjectBatch<Order, GatewayEpollReceiveBatchSize>;
    using OrderBatchQueue =
        SpscQueue<OrderBatch *, GatewayEpollReceiveQueueSize>;
    using OrderBatchConsumer =
        SpscQueueConsumer<OrderBatch *, GatewayEpollReceiveQueueSize>;
    using OrderBatchProducer =
        SpscQueueProducer<OrderBatch *, GatewayEpollReceiveQueueSize>;
    using OrderBatchMempool = StoragePool<OrderBatch, GatewayOrderPoolSize>;
    using OrderConfirmationQueue =
        SpscQueue<ClientMessage, GatewayOrderConfirmationQueueSize>;
    using OrderConfirmationProducer =
        SpscQueueProducer<ClientMessage, GatewayOrderConfirmationQueueSize>;
    using OrderConfirmationConsumer =
        SpscQueueConsumer<ClientMessage, GatewayOrderConfirmationQueueSize>;
    // Resend buffers: unsent orders per asset, unsent confirmations per fd.
    using OrderResendBuffer =
        BytesBuffer<GatewayEpollReceiveBatchSize * sizeof(Order)>;
    using ConfirmationResendBuffer =
        BytesBuffer<GatewayEpollReceiveBatchSize * sizeof(OrderConfirmation)>;

    // ------------------------------------------------ trade reports in
    using TradeReportQueue =
        SpscQueue<OrderStateReport *, TradeReportReceiveQueueSize>;
    using TradeReportMempool =
        StoragePool<OrderStateReport, TradeReportReceivePoolSize>;
    using TradeReportBatch =
        std::array<OrderStateReport *, TradeReportReceiveBatchSize>;
    using TradeReportUpdatesBuffer =
        std::array<OrderStateReport, GatewayUpdateBufferSize>;
    using TradeReportMulticastReceiver =
        UdpMulticastReceiver<OrderStateReport, TradeReportReceiveBufferSize,
                             TradeReportReceiveQueueSize,
                             TradeReportReceivePoolSize,
                             TradeReportReceiveBatchSize>;

    // ---------------------------------------- account service round trip
    using AuthRequestQueue =
        SpscQueue<RoutedAuthRequest *, GatewayRequestQueueSize>;
    using AuthRequestProducer =
        SpscQueueProducer<RoutedAuthRequest *, GatewayRequestQueueSize>;
    using AuthRequestMempool =
        StoragePool<RoutedAuthRequest, GatewayRequestPoolSize>;

    using AccountResponseBatch =
        ObjectBatch<ClientAccountSnapshot,
                    GatewayClientRequestResponseBatchSize>;
    using AccountResponseQueue =
        SpscQueue<AccountResponseBatch *,
                  GatewayClientRequestResponseQueueSize>;
    using AccountResponseProducer =
        SpscQueueProducer<AccountResponseBatch *,
                          GatewayClientRequestResponseQueueSize>;
    using AccountResponseMempool =
        StoragePool<AccountResponseBatch, GatewayClientRequestResponsePoolSize>;

    // ------------------------------------------------------- disconnects
    using DisconnectQueue = SpscQueue<uint32_t, GatewayMaxClients>;
    using DisconnectProducer = SpscQueueProducer<uint32_t, GatewayMaxClients>;
    using DisconnectConsumer = SpscQueueConsumer<uint32_t, GatewayMaxClients>;

    // client id -> the fd that client is connected on
    using ClientFdMap = FlatHashMap<uint32_t, uint32_t, GatewayMaxClients>;

    // ---------------------------------------------------- CRTP base types
    // Forward declarations: the bases below are parameterised on the
    // derived class, which is only completed in its own header.
    class GatewayServer;
    class GatewayRequestForwarder;
    class ClientStatesWriter;

    using GatewayServerBase =
        EpollServer<GatewayServer, Order, GatewayEpollReceiveBatchSize,
                    GatewayEpollReceiveQueueSize, GatewayOrderPoolSize,
                    ClientAuthRequest>;

    using GatewayRequestForwarderBase =
        Consumer<GatewayRequestForwarder, RoutedAuthRequest,
                 GatewayRequestQueueSize, GatewayRequestPoolSize>;

    using StatesWriterReportBase =
        Consumer<ClientStatesWriter, OrderStateReport,
                 TradeReportReceiveQueueSize, TradeReportReceivePoolSize,
                 TradeReportReceiveBatchSize>;
    using StatesWriterResponseBase =
        Consumer<ClientStatesWriter, AccountResponseBatch,
                 GatewayClientRequestResponseQueueSize,
                 GatewayClientRequestResponsePoolSize>;
} // namespace naoto::order_gateway
