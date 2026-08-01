#pragma once

#include <ClientRequest.hpp>
#include <ClientRequestResponse.hpp>
#include <ClientStates.hpp>
#include <ClientStatesKeeper.hpp>
#include <ClientStatesWriter.hpp>
#include <EpollServer.hpp>
#include <GatewayWriter.hpp>
#include <MarketUpdates.hpp>
#include <OrderStateReport.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
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
        // TODO: Add the FdGen update in epoll server
        std::array<FdGen, MaxGateways> GatewayFd;
        EpollServer<ClientRequest, BatchSize> Server;
        ClientStatesKeeper<MaxPositions, BatchSize> Keeper;
        GatewayWriter<MaxPositions, BatchSize, MaxGateways, 128> MessageWriter;
        ClientStatesWriter<MaxPositions, BatchSize> Writer;
        MarketUpdates<UdpReceiveBufferSize, BatchSize> ReportReceiver;

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
            , Server(serverPort, maxEvents, maxPending, RequestPool, Requests)
            , Keeper(States, Requests, Responses, RequestPool, ResponsePool)
            , MessageWriter(Responses, ResponsePool, GatewayFd)
            , Writer(States, Reports, ReportPool)
            , ReportReceiver(argc, argv, Reports, ReportPool, portId,
                             nbRxQueueSlots, dpdkPoolSize, dstIp, dstPort)
        {}

        void StartClientDetailsProvider(void) noexcept
        {
            assert(rte_lcore_id() == rte_get_main_lcore()
                   && "StartClientDetailsProvider must run on the thread that "
                      "constructed "
                      "ClientDetailsProvider (the DPDK main lcore)");

            std::thread serverThread(
                &EpollServer<ClientRequest, BatchSize>::startServer, &Server);

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
