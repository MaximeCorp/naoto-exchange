#pragma once

#include <account_request_sender.hpp>
#include <account_service_response_receiver.hpp>
#include <client_account_snapshot.hpp>
#include <client_states.hpp>
#include <client_states_writer.hpp>
#include <file_descriptors_ops.hpp>
#include <routed_auth_request.hpp>
#include <gateway_server.hpp>
#include <trade_report_receiver.hpp>
#include <netinet/tcp.h>
#include <object_batch.hpp>
#include <order.hpp>
#include <pthread.h>
#include <readerwritercircularbuffer.h>
#include <order_router.hpp>
#include <thread>

namespace naoto::order_gateway
{
    template <size_t BatchSize, size_t MaxAsset, size_t MaxPositions,
              size_t MaxClients, size_t UpdatesBufferSize,
              size_t ReceiveRingBufferSize>
    class OrderGatewayService
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;
        using DisconnectsQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<uint32_t>;
        using ReportQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;
        using ResponseBatch =
            ObjectBatch<ClientAccountSnapshot<MaxPositions>, BatchSize>;

        using ResponseQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<ResponseBatch *>;
        using RequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<RoutedAuthRequest *>;

    private:
        StoragePool<ObjectBatch<Order, BatchSize>> OrdersPool;
        StoragePool<OrderStateReport> ReportsPool;
        StoragePool<ResponseBatch> ResponsesPool;
        StoragePool<RoutedAuthRequest> ServerRequestsPool;
        StoragePool<RoutedAuthRequest> WriterRequestsPool;
        OrdersQueue IncomingOrders;
        DisconnectsQueue IncomingDisconnects;
        ReportQueue IncomingReports;
        ResponseQueue IncomingResponses;
        RequestQueue ServerIncomingRequests;
        RequestQueue WriterIncomingRequests;
        ClientStates<MaxPositions> States;
        VersionedFd ClientStatesUpdatesFd;
        GatewayServer<BatchSize, MaxPositions> Server;
        OrderRouter<BatchSize, MaxAsset, MaxPositions, MaxClients> Router;
        ClientStatesWriter<MaxClients, MaxPositions, BatchSize,
                           UpdatesBufferSize>
            StatesWriter;
        TradeReportReceiver<ReceiveRingBufferSize, BatchSize> UpdatesReceiver;
        AccountServiceResponseReceiver<BatchSize, MaxPositions>
            AccountReceiver;
        AccountRequestSender RequestSender;

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
        OrderGatewayService(int argc, char **argv, const uint16_t portId,
                      const uint16_t nbRxQueueSlots, const size_t poolSize,
                      const uint32_t dstIp, const uint16_t dstPort,
                      const size_t queue_size, const int port,
                      const int maxEvents, const int maxPending)
            : OrdersPool(poolSize)
            , ReportsPool(poolSize)
            , ResponsesPool(poolSize)
            , ServerRequestsPool(poolSize)
            , WriterRequestsPool(poolSize)
            , IncomingOrders(queue_size)
            , IncomingDisconnects(queue_size)
            , IncomingReports(queue_size)
            , IncomingResponses(queue_size)
            , ServerIncomingRequests(queue_size)
            , WriterIncomingRequests(queue_size)
            , States(MaxClients)
            , Server(port, maxEvents, maxPending, OrdersPool, IncomingOrders,
                     States, ClientStatesUpdatesFd, IncomingDisconnects,
                     ServerRequestsPool, ServerIncomingRequests)
            , Router(OrdersPool, IncomingOrders, ClientStatesUpdatesFd, States)
            , StatesWriter(States, IncomingReports, ReportsPool,
                           IncomingResponses, ResponsesPool,
                           IncomingDisconnects, WriterIncomingRequests,
                           WriterRequestsPool)
            , UpdatesReceiver(argc, argv, IncomingReports, ReportsPool, portId,
                              nbRxQueueSlots, poolSize, dstIp, dstPort)
            , AccountReceiver(ClientStatesUpdatesFd, IncomingResponses,
                              ResponsesPool)
            , RequestSender(ServerIncomingRequests, WriterIncomingRequests,
                            ServerRequestsPool, WriterRequestsPool,
                            ClientStatesUpdatesFd)
        {
            FileDescriptorsOps::setMaxFd(MaxClients);
        }

        void StartGateway(void)
        {
            std::thread riskThread(
                &OrderRouter<BatchSize, MaxAsset, MaxPositions,
                             MaxClients>::startLoop,
                &Router);
            std::thread serverThread(
                &GatewayServer<BatchSize, MaxPositions>::startServer, &Server);
            std::thread StatesWriterThread(
                &ClientStatesWriter<MaxClients, MaxPositions, BatchSize,
                                    UpdatesBufferSize>::StartLoop,
                &StatesWriter);

            std::thread UpdatesReceiverThread(
                &TradeReportReceiver<ReceiveRingBufferSize,
                               BatchSize>::StartReceiversLoop,
                &UpdatesReceiver);

            std::thread AccountReceiverThread(
                &AccountServiceResponseReceiver<BatchSize,
                                                MaxPositions>::StartLoop,
                &AccountReceiver);

            std::thread RequestSenderThread(&AccountRequestSender::StartLoop,
                                            &RequestSender);

            // TODO: Set affinity and thread names
            setAffinity(riskThread, 2); // Hard coded for local tests
            setAffinity(serverThread, 4);

            pthread_setname_np(riskThread.native_handle(), "RiskEngine");
            pthread_setname_np(serverThread.native_handle(), "EpollServer");

            riskThread.join();
            serverThread.join();
        }
    };
} // namespace naoto::order_gateway
