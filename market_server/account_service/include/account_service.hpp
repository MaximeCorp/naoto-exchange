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
#include <nlohmann/json.hpp>
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

        void setAffinity(std::thread &t, const int core_id)
        {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id, &cpuset);

            int rc = pthread_setaffinity_np(t.native_handle(),
                                            sizeof(cpu_set_t), &cpuset);

            if (rc != 0)
            {
                std::cerr << "Error setting affinity: " << rc << std::endl;
            }
            else
            {
                std::cout << "Thread successfully pinned to CPU " << core_id
                          << std::endl;
            }
        }

        void EtcdClientSetUp(void)
        {
            const char *etcd_addr =
                std::getenv("ETCD_ADDR") ?: "localhost:2379";
            const char *listen = std::getenv("LISTEN_ADDR") ?: "127.0.0.1:8082";

            EtcdClient = std::make_unique<etcd::SyncClient>(etcd_addr);

            KeepAlive = EtcdClient->leasekeepalive(10);
            int64_t lid = KeepAlive->Lease();

            std::string key = std::string("/account-service/0");

            EtcdClient->set(
                key,
                nlohmann::json({ { "addr", listen }, { "status", "active" } })
                    .dump(),
                lid);
        }

    public:
        AccountService(int argc, char **argv, int serverPort, int maxEvents,
                       int maxPending, const uint16_t portId,
                       const uint16_t nbRxQueueSlots, const size_t dpdkPoolSize,
                       const uint32_t dstIp, const uint16_t dstPort)
            : States(MaxClients)
            , RequestPool(MaxClients)
            , ReportPool(MaxClients * MaxTradeClient)
            , ResponsePool(MaxClients)
            , Requests(MaxClients)
            , Reports(MaxClients)
            , Responses(MaxClients)
            , Server(serverPort, maxEvents, maxPending, RequestPool, Requests,
                     GatewayFd)
            , Processor(States, Requests, Responses, RequestPool, ResponsePool)
            , Dispatcher(Responses, ResponsePool, GatewayFd)
            , Writer(States, Reports, ReportPool)
            , ReportReceiver(argc, argv, Reports, ReportPool, portId,
                             nbRxQueueSlots, dpdkPoolSize, dstIp, dstPort)
        {}

        AccountService(int argc, char **argv, int serverPort, int maxEvents,
                       int maxPending, const uint16_t portId,
                       const uint16_t nbRxQueueSlots, const size_t dpdkPoolSize,
                       const uint32_t dstIp, const uint16_t dstPort,
                       std::vector<ClientState<MaxPositions>> &clients)
            : States(MaxClients, clients)
            , RequestPool(MaxClients)
            , ReportPool(MaxClients * MaxTradeClient)
            , ResponsePool(MaxClients)
            , Requests(MaxClients)
            , Reports(MaxClients)
            , Responses(MaxClients)
            , Server(serverPort, maxEvents, maxPending, RequestPool, Requests,
                     GatewayFd)
            , Processor(States, Requests, Responses, RequestPool, ResponsePool)
            , Dispatcher(Responses, ResponsePool, GatewayFd)
            , Writer(States, Reports, ReportPool)
            , ReportReceiver(argc, argv, Reports, ReportPool, portId,
                             nbRxQueueSlots, dpdkPoolSize, dstIp, dstPort)
        {}

        ~AccountService()
        {
            if (KeepAlive)
            {
                KeepAlive->Cancel();
            }
        }

        void StartAccountService(void) noexcept
        {
            assert(rte_lcore_id() == rte_get_main_lcore()
                   && "StartAccountService must run on the thread that "
                      "constructed "
                      "AccountService (the DPDK main lcore)");

            EtcdClientSetUp();

            std::thread serverThread(
                &GatewayEpollServer<AccountEpollReceiveBatchSize,
                                    MaxGateways>::startServer,
                &Server);

            std::thread keeperThread(&ClientRequestProcessor::StartLoop,
                                     &Processor);

            std::thread dispatcherThread(
                &GatewayResponseDispatcher<
                    MaxPositions, AccountResponseBatchSize, MaxGateways,
                    AccountResponsesResendBufferSize>::StartLoop,
                &Dispatcher);

            std::thread writerThread(
                &ClientStatesWriter<MaxPositions,
                                    ClientStatesSwapBatchSize>::StartLoop,
                &Writer);

            setAffinity(serverThread, 7);
            setAffinity(keeperThread, 8);
            setAffinity(dispatcherThread, 9);
            setAffinity(writerThread, 10);

            ReportReceiver.StartReceiversLoop();

            serverThread.join();
            keeperThread.join();
            dispatcherThread.join();
            writerThread.join();
        }
    };
} // namespace naoto::account_service
