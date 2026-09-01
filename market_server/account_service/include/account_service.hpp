#pragma once

#include <client_account_snapshot.hpp>
#include <client_request_processor.hpp>
#include <client_state.hpp>
#include <client_states.hpp>
#include <client_states_writer.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <gateway_epoll_server.hpp>
#include <gateway_response_dispatcher.hpp>
#include <memory>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <thread>
#include <trade_report_receiver.hpp>

namespace naoto::account_service
{
    class AccountService
    {
        using RequestQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize> *>;
        using ReportQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;
        using ResponseQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            RoutedMessage<ClientAccountSnapshot<MaxPositions>>>;

    private:
        ClientStates<MaxPositions> States;
        StoragePool<
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize>>
            RequestPool;
        StoragePool<OrderStateReport> ReportPool;
        StoragePool<ClientAccountSnapshot<MaxPositions>> ResponsePool;
        RequestQueue Requests;
        ReportQueue Reports;
        ResponseQueue Responses;
        std::array<VersionedFd, MaxGateways> GatewayFd;
        GatewayEpollServer<AccountEpollReceiveBatchSize, MaxGateways> Server;
        ClientRequestProcessor Processor;
        GatewayResponseDispatcher<MaxPositions, AccountResponseBatchSize,
                                  MaxGateways, AccountResponsesResendBufferSize>
            Dispatcher;
        ClientStatesWriter<MaxPositions, ClientStatesSwapBatchSize> Writer;
        TradeReportReceiver<TradeReportReceiveBufferSize,
                            TradeReportReceiveBatchSize>
            ReportReceiver;

        std::shared_ptr<etcd::KeepAlive> KeepAlive;
        std::unique_ptr<etcd::SyncClient> EtcdClient;

        // Startup-only: pins service threads once at launch, never called
        // again afterwards. Defined out of line in account_service.cpp.
        void setAffinity(std::thread &t, const int core_id);

        // Startup-only: registers this instance with etcd once at launch.
        // Defined out of line in account_service.cpp.
        void EtcdClientSetUp(void);

    public:
        AccountService(int argc, char **argv, int serverPort, int maxEvents,
                       int maxPending, const uint16_t portId,
                       const uint16_t nbRxQueueSlots, const size_t dpdkPoolSize,
                       const uint32_t dstIp, const uint16_t dstPort);

        AccountService(int argc, char **argv, int serverPort, int maxEvents,
                       int maxPending, const uint16_t portId,
                       const uint16_t nbRxQueueSlots, const size_t dpdkPoolSize,
                       const uint32_t dstIp, const uint16_t dstPort,
                       std::vector<ClientState<MaxPositions>> &clients);

        ~AccountService();

        // Startup only: spins up and joins the service's threads. Not on
        // the hot path itself (the hot loops it launches stay wherever
        // they already lived). Defined out of line in account_service.cpp.
        void StartAccountService(void) noexcept;
    };
} // namespace naoto::account_service
