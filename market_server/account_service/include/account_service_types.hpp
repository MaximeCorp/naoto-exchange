#pragma once

#include <array>
#include <client_account_snapshot.hpp>
#include <consumer.hpp>
#include <cstdint>
#include <epoll_server.hpp>
#include <gateway_handshake.hpp>
#include <object_batch.hpp>
#include <order_state_report.hpp>
#include <routed_auth_request.hpp>
#include <routed_message.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <udp_multicast_receiver.hpp>
#include <versioned_fd.hpp>

// Every concrete type the account service is built out of, in one place.
// Changing a queue depth, a batch size or the object a pool holds is a
// one-line edit here (or in system_conf.hpp) instead of a hunt through
// every class that names the instantiation.
namespace naoto::account_service
{
    // -------------------------------------------- auth requests from gateways
    using AuthRequestBatch =
        ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize>;
    using AuthRequestQueue =
        SpscQueue<AuthRequestBatch *, AccountEpollReceiveQueueSize>;
    using AuthRequestConsumer =
        SpscQueueConsumer<AuthRequestBatch *, AccountEpollReceiveQueueSize>;
    using AuthRequestMempool =
        StoragePool<AuthRequestBatch, AccountRequestPoolSize>;

    // ------------------------------------------- snapshots back to gateways
    using AccountResponse = RoutedMessage<ClientAccountSnapshot>;
    using AccountResponseQueue = SpscQueue<AccountResponse, MaxClients>;
    using AccountResponseProducer =
        SpscQueueProducer<AccountResponse, MaxClients>;
    using AccountResponseConsumer =
        SpscQueueConsumer<AccountResponse, MaxClients>;
    using AccountResponseMempool =
        StoragePool<ClientAccountSnapshot, AccountResponsePoolSize>;
    using AccountResponseResendBuffer =
        std::array<AccountResponse, AccountResponsesResendBufferSize>;

    // -------------------------------------------------- trade reports in
    using TradeReportQueue =
        SpscQueue<OrderStateReport *, TradeReportReceiveQueueSize>;
    using TradeReportMempool =
        StoragePool<OrderStateReport, TradeReportReceivePoolSize>;
    using TradeReportBatch =
        std::array<OrderStateReport *, TradeReportReceiveBatchSize>;
    using TradeReportMulticastReceiver =
        UdpMulticastReceiver<OrderStateReport, TradeReportReceiveBufferSize,
                             TradeReportReceiveQueueSize,
                             TradeReportReceivePoolSize,
                             TradeReportReceiveBatchSize>;

    // One connection slot per gateway this service serves.
    using GatewayFds = std::array<VersionedFd, MaxGateways>;

    // ---------------------------------------------------- CRTP base types
    // Forward declarations: the bases below are parameterised on the
    // derived class, which is only completed in its own header.
    class GatewayEpollServer;
    class ClientStatesWriter;

    using GatewayEpollServerBase =
        EpollServer<GatewayEpollServer, RoutedAuthRequest,
                    AccountEpollReceiveBatchSize, AccountEpollReceiveQueueSize,
                    AccountRequestPoolSize, GatewayHandshake>;

    using ClientStatesWriterBase =
        Consumer<ClientStatesWriter, OrderStateReport,
                 TradeReportReceiveQueueSize, TradeReportReceivePoolSize,
                 TradeReportReceiveBatchSize>;
} // namespace naoto::account_service
