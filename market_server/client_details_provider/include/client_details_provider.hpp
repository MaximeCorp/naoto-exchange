#pragma once

#include <client_details_provider_server.hpp>
#include <client_request.hpp>
#include <client_request_response.hpp>
#include <client_state.hpp>
#include <client_states.hpp>
#include <client_states_keeper.hpp>
#include <client_states_writer.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/SyncClient.hpp>
#include <gateway_writer.hpp>
#include <market_updates.hpp>
#include <nlohmann/json.hpp>
#include <order_state_report.hpp>
#include <reader_writer_circular_buffer.hpp>
#include <storage_pool.hpp>
#include <thread>

namespace AccountService
{
    template <size_t BatchSize, size_t MaxPositions,
              size_t UdpReceiveBufferSize, size_t MaxGateways>
    class ClientDetailsProvider
    {
        using RequestQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<ClientRequest, BatchSize> *>;
        using ReportQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;
        using ResponseQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            MessageContainer<ClientRequestResponse<MaxPositions>>>;

    private:
        ClientStates<MaxPositions> States;
        StoragePool<ObjectBatch<ClientRequest, BatchSize>> RequestPool;
        StoragePool<OrderStateReport> ReportPool;
        StoragePool<ClientRequestResponse<MaxPositions>> ResponsePool;
        RequestQueue Requests;
        ReportQueue Reports;
        ResponseQueue Responses;
        std::array<FdGen, MaxGateways> GatewayFd;
        ClientDetailsProviderServer<BatchSize, MaxGateways> Server;
        ClientStatesKeeper<MaxPositions, BatchSize> Keeper;
        GatewayWriter<MaxPositions, BatchSize, MaxGateways, 128> MessageWriter;
        ClientStatesWriter<MaxPositions, BatchSize> Writer;
        MarketUpdates<UdpReceiveBufferSize, BatchSize> ReportReceiver;

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

            std::string key = std::string("/client-details-provider/0");

            EtcdClient->set(
                key,
                nlohmann::json({ { "addr", listen }, { "status", "active" } })
                    .dump(),
                lid);
        }

    public:
        ClientDetailsProvider(int argc, char **argv, size_t maxClients,
                              size_t requestPoolSize, size_t reportPoolSize,
                              size_t responsePoolSize, size_t requestQueueSize,
                              size_t reportQueueSize, size_t responseQueueSize,
                              int serverPort, int maxEvents, int maxPending,
                              const uint16_t portId,
                              const uint16_t nbRxQueueSlots,
                              const size_t dpdkPoolSize, const uint32_t dstIp,
                              const uint16_t dstPort)
            : States(maxClients)
            , RequestPool(requestPoolSize)
            , ReportPool(reportPoolSize)
            , ResponsePool(responsePoolSize)
            , Requests(requestQueueSize)
            , Reports(reportQueueSize)
            , Responses(responseQueueSize)
            , Server(serverPort, maxEvents, maxPending, RequestPool, Requests,
                     GatewayFd)
            , Keeper(States, Requests, Responses, RequestPool, ResponsePool)
            , MessageWriter(Responses, ResponsePool, GatewayFd)
            , Writer(States, Reports, ReportPool)
            , ReportReceiver(argc, argv, Reports, ReportPool, portId,
                             nbRxQueueSlots, dpdkPoolSize, dstIp, dstPort)
        {}

        ClientDetailsProvider(int argc, char **argv, size_t maxClients,
                              size_t requestPoolSize, size_t reportPoolSize,
                              size_t responsePoolSize, size_t requestQueueSize,
                              size_t reportQueueSize, size_t responseQueueSize,
                              int serverPort, int maxEvents, int maxPending,
                              const uint16_t portId,
                              const uint16_t nbRxQueueSlots,
                              const size_t dpdkPoolSize, const uint32_t dstIp,
                              const uint16_t dstPort,
                              std::vector<ClientState<MaxPositions>> &clients)
            : States(maxClients, clients)
            , RequestPool(requestPoolSize)
            , ReportPool(reportPoolSize)
            , ResponsePool(responsePoolSize)
            , Requests(requestQueueSize)
            , Reports(reportQueueSize)
            , Responses(responseQueueSize)
            , Server(serverPort, maxEvents, maxPending, RequestPool, Requests,
                     GatewayFd)
            , Keeper(States, Requests, Responses, RequestPool, ResponsePool)
            , MessageWriter(Responses, ResponsePool, GatewayFd)
            , Writer(States, Reports, ReportPool)
            , ReportReceiver(argc, argv, Reports, ReportPool, portId,
                             nbRxQueueSlots, dpdkPoolSize, dstIp, dstPort)
        {}

        ~ClientDetailsProvider()
        {
            if (KeepAlive)
            {
                KeepAlive->Cancel();
            }
        }

        void StartClientDetailsProvider(void) noexcept
        {
            assert(rte_lcore_id() == rte_get_main_lcore()
                   && "StartClientDetailsProvider must run on the thread that "
                      "constructed "
                      "ClientDetailsProvider (the DPDK main lcore)");

            EtcdClientSetUp();

            std::thread serverThread(
                &ClientDetailsProviderServer<BatchSize,
                                             MaxGateways>::startServer,
                &Server);

            std::thread keeperThread(
                &ClientStatesKeeper<MaxPositions, BatchSize>::StartLoop,
                &Keeper);

            std::thread messageWriterThread(
                &GatewayWriter<MaxPositions, BatchSize, 32, 128>::StartLoop,
                &MessageWriter);

            std::thread writerThread(
                &ClientStatesWriter<MaxPositions, BatchSize>::StartLoop,
                &Writer);

            setAffinity(serverThread, 7);
            setAffinity(keeperThread, 8);
            setAffinity(messageWriterThread, 9);
            setAffinity(writerThread, 10);

            ReportReceiver.StartReceiversLoop();

            serverThread.join();
            keeperThread.join();
            messageWriterThread.join();
            writerThread.join();
        }
    };
} // namespace AccountService
