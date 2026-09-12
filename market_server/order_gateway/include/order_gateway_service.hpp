#pragma once

#include <account_request_sender.hpp>
#include <account_service_response_receiver.hpp>
#include <chrono>
#include <client_account_snapshot.hpp>
#include <client_message_sender.hpp>
#include <client_states.hpp>
#include <client_states_writer.hpp>
#include <file_descriptors_ops.hpp>
#include <gateway_server.hpp>
#include <netinet/tcp.h>
#include <object_batch.hpp>
#include <order.hpp>
#include <order_gateway_types.hpp>
#include <order_router.hpp>
#include <pthread.h>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <shared_memory_ops.hpp>
#include <shared_memory_types.hpp>
#include <system_conf.hpp>
#include <thread>
#include <trade_report_receiver.hpp>

namespace naoto::order_gateway
{
    class OrderGatewayService
    {
    private:
        OrderBatchMempool OrdersPool;
        TradeReportMempool ReportsPool;
        AccountResponseMempool ResponsesPool;
        AuthRequestMempool ServerRequestsPool;
        AuthRequestMempool WriterRequestsPool;
        OrderBatchQueue IncomingOrders;
        DisconnectQueue IncomingDisconnects;
        TradeReportQueue IncomingReports;
        AccountResponseQueue IncomingResponses;
        AuthRequestQueue ServerIncomingRequests;
        AuthRequestQueue WriterIncomingRequests;
        OrderConfirmationQueue IncomingConfirmation;
        ClientStates States;
        VersionedFd ClientStatesUpdatesFd;
        GatewayServer Server;
        ClientMessageSender ConfirmationSender;
        OrderRouter Router;
        ClientStatesWriter StatesWriter;
        TradeReportReceiver UpdatesReceiver;
        AccountServiceResponseReceiver AccountReceiver;
        AccountRequestSender RequestSender;

        [[nodiscard]] OrderQueue *GetSharedQueue(void)
        {
            OrderQueue *queue = nullptr;
            while (!queue)
            {
                queue = OpenSharedQueue();
                if (queue == nullptr)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }

            std::cout << "Got the shared queue: " << (uintptr_t)queue
                      << "bytes size:" << sizeof(OrderQueue) << "\n\n";

            return queue;
        }

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
                            const uint16_t nbRxQueueSlots,
                            const size_t poolSize, const uint32_t dstIp,
                            const uint16_t dstPort, const int port,
                            const int maxEvents, const int maxPending)
            : States(GatewayMaxClients)
            , Server(port, maxEvents, maxPending, OrdersPool, &IncomingOrders,
                     States, ClientStatesUpdatesFd, &IncomingDisconnects,
                     ServerRequestsPool, &ServerIncomingRequests)
            , ConfirmationSender(&IncomingConfirmation)
            , Router(OrdersPool, &IncomingOrders, ClientStatesUpdatesFd, States,
                     &IncomingConfirmation
#ifdef NAOTO_SHARED_MEMORY
                     ,
                     GetSharedQueue()
#endif
                         )
            , StatesWriter(States, &IncomingReports, ReportsPool,
                           &IncomingResponses, ResponsesPool,
                           &IncomingDisconnects, &WriterIncomingRequests,
                           WriterRequestsPool)
            , UpdatesReceiver(argc, argv, &IncomingReports, ReportsPool, portId,
                              nbRxQueueSlots, poolSize, dstIp, dstPort)
            , AccountReceiver(ClientStatesUpdatesFd, &IncomingResponses,
                              ResponsesPool)
            , RequestSender(&ServerIncomingRequests, &WriterIncomingRequests,
                            ServerRequestsPool, WriterRequestsPool,
                            ClientStatesUpdatesFd)
        {
            FileDescriptorsOps::setMaxFd(GatewayMaxClients);
        }

        void StartGateway(void)
        {
            std::thread riskThread(&OrderRouter::startLoop, &Router);
            std::thread serverThread(&GatewayServer::startServer, &Server);
            std::thread statesWriterThread(&ClientStatesWriter::StartLoop,
                                           &StatesWriter);

            std::thread confirmationSenderThread(
                &ClientMessageSender::StartLoop, &ConfirmationSender);

            std::thread updatesReceiverThread(
                &TradeReportReceiver::StartReceiversLoop, &UpdatesReceiver);

            std::thread accountReceiverThread(
                &AccountServiceResponseReceiver::StartLoop, &AccountReceiver);

            std::thread requestSenderThread(&AccountRequestSender::StartLoop,
                                            &RequestSender);

            // TODO: Set affinity and thread names
            setAffinity(riskThread, 4); // Hard coded for local tests
            setAffinity(serverThread, 2);
            setAffinity(statesWriterThread, 10);
            setAffinity(updatesReceiverThread, 11);
            setAffinity(accountReceiverThread, 10);
            setAffinity(requestSenderThread, 11);
            setAffinity(confirmationSenderThread, 10);

            pthread_setname_np(riskThread.native_handle(), "RiskEngine");
            pthread_setname_np(serverThread.native_handle(), "EpollServer");

            riskThread.join();
            serverThread.join();
        }
    };
} // namespace naoto::order_gateway
