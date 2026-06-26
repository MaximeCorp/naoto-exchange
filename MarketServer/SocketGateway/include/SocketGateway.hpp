#pragma once

#include <ClientStates.hpp>
#include <ClientStatesInjector.hpp>
#include <EpollServer.hpp>
#include <FileDescriptorsOps.hpp>
#include <ObjectBatch.hpp>
#include <Order.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <RiskService.hpp>
#include <netinet/tcp.h>
#include <pthread.h>
#include <thread>

namespace Gateways
{
    template <size_t BatchSize, size_t MaxAsset>
    class SocketGateway
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;

    private:
        int ClientStatesUpdatesFd;
        EpollServer<Order, BatchSize> Server;
        ClientStates ClientsInfo;
        RiskService<BatchSize, MaxAsset> Risk;
        ClientStatesInjector ClientsInfoInjector;

        StoragePool<ObjectBatch<Order, BatchSize>> OrdersPool;
        OrdersQueue IncomingOrders;

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

        [[nodiscard]] int
        connectClientStatesUpdateService(const std::string &ip, const int port)
        {
            int res = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

            int one = 1;
            setsockopt(ClientStatesUpdatesFd, IPPROTO_TCP, TCP_NODELAY, &one,
                       sizeof(one));

            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (connect(ClientStatesUpdatesFd, (struct sockaddr *)&addr,
                        sizeof(addr))
                < 0)
            {
                if (errno != EINPROGRESS)
                    std::cerr << "Bro you didn't connect to risk service.\n";
            }

            return res;
        }

    public:
        SocketGateway(const size_t max_clients, const size_t queue_size,
                      const int port, const int maxEvents, const int maxPending,
                      const std::string &clientStatesUpdatesIp,
                      const int clientStatesUpdatesPort,
                      const std::string &marketUpdatesIp,
                      const int marketUpdatesPort, const size_t nb_fds)
            : ClientStatesUpdatesFd(connectClientStatesUpdateService(
                clientStatesUpdatesIp, clientStatesUpdatesPort))
            , Server(port, maxEvents, maxPending, OrdersPool, IncomingOrders,
                     ClientStatesUpdatesFd, nb_fds)
            , ClientsInfo(nb_fds)
            , Risk(OrdersPool, IncomingOrders, ClientsInfo)
            , ClientsInfoInjector(ClientsInfo, ClientStatesUpdatesFd,
                                  marketUpdatesIp, marketUpdatesPort)
            , OrdersPool(max_clients)
            , IncomingOrders(queue_size)
        {}

        SocketGateway(const size_t max_clients, const size_t queue_size,
                      const int port, const int maxEvents, const int maxPending,
                      const std::string &clientStatesUpdatesIp,
                      const int clientStatesUpdatesPort)
            : Server(port, maxEvents, maxPending, OrdersPool, IncomingOrders)
            , ClientsInfo(FileDescriptorsOps::getMaxFd())
            , Risk(OrdersPool, IncomingOrders, ClientsInfo)
            , OrdersPool(max_clients)
            , IncomingOrders(queue_size)
        {
            connectClientStatesUpdateService(clientStatesUpdatesIp,
                                             clientStatesUpdatesPort);
        }

        ~SocketGateway(void)
        {
            close(ClientStatesUpdatesFd);
        }

        void StartGateway(void)
        {
            std::thread riskThread(&RiskService<BatchSize, MaxAsset>::startLoop,
                                   &Risk);
            std::thread serverThread(
                &EpollServer<Order, BatchSize>::startServer, &Server);

            setAffinity(riskThread, 2); // Hard coded for local tests
            setAffinity(serverThread, 4);

            pthread_setname_np(riskThread.native_handle(), "RiskEngine");
            pthread_setname_np(serverThread.native_handle(), "EpollServer");

            riskThread.join();
            serverThread.join();
        }
    };
} // namespace Gateways
