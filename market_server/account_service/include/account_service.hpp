#pragma once

#include <account_service_types.hpp>
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
#include <vector>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <thread>
#include <trade_report_receiver.hpp>

namespace naoto::account_service
{
    class AccountService
    {
    private:
        ClientStates States;
        AuthRequestMempool RequestPool;
        TradeReportMempool ReportPool;
        AccountResponseMempool ResponsePool;
        AuthRequestQueue Requests;
        TradeReportQueue Reports;
        AccountResponseQueue Responses;
        GatewayFds GatewayFd;
        GatewayEpollServer Server;
        ClientRequestProcessor Processor;
        GatewayResponseDispatcher Dispatcher;
        ClientStatesWriter Writer;
        TradeReportReceiver ReportReceiver;

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
                       std::vector<ClientState> &clients);

        ~AccountService();

        // Startup only: spins up and joins the service's threads. Not on
        // the hot path itself (the hot loops it launches stay wherever
        // they already lived). Defined out of line in account_service.cpp.
        void StartAccountService(void) noexcept;
    };
} // namespace naoto::account_service
