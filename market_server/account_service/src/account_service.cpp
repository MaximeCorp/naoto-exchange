#include <account_service.hpp>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

namespace naoto::account_service
{
    void AccountService::setAffinity(std::thread &t, const int core_id)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        int rc = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t),
                                        &cpuset);

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

    void AccountService::EtcdClientSetUp(void)
    {
        const char *etcd_addr = std::getenv("ETCD_ADDR") ?: "localhost:2379";
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

    AccountService::AccountService(int argc, char **argv, int serverPort,
                                   int maxEvents, int maxPending,
                                   const uint16_t portId,
                                   const uint16_t nbRxQueueSlots,
                                   const size_t dpdkPoolSize,
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

    AccountService::AccountService(
        int argc, char **argv, int serverPort, int maxEvents, int maxPending,
        const uint16_t portId, const uint16_t nbRxQueueSlots,
        const size_t dpdkPoolSize, const uint32_t dstIp, const uint16_t dstPort,
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

    AccountService::~AccountService()
    {
        if (KeepAlive)
        {
            KeepAlive->Cancel();
        }
    }

    void AccountService::StartAccountService(void) noexcept
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

        setAffinity(serverThread, 10);
        setAffinity(keeperThread, 11);
        setAffinity(dispatcherThread, 11);
        setAffinity(writerThread, 10);

        ReportReceiver.StartReceiversLoop();

        serverThread.join();
        keeperThread.join();
        dispatcherThread.join();
        writerThread.join();
    }
} // namespace naoto::account_service
