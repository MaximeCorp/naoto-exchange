#pragma once

#include <EpollServer.hpp>
#include <Order.hpp>
#include <OrderBatch.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <RiskService.hpp>
#include <pthread.h>
#include <thread>

namespace Gateways
{
    template <size_t BatchSize>
    class SocketGateway
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        EpollServer<BatchSize> Server;
        RiskService<BatchSize> Risk;

        StoragePool<OrderBatch<BatchSize>> OrdersPool;
        OrdersQueue IncomingOrders;

        void setAffinity(std::thread &t, int core_id)
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
        SocketGateway(size_t max_clients, size_t queue_size, int port,
                      int maxEvents, int maxPending, size_t nb_fds)
            : Server(port, maxEvents, maxPending, OrdersPool, IncomingOrders,
                     nb_fds)
            , Risk(OrdersPool, IncomingOrders, nb_fds)
            , OrdersPool(max_clients)
            , IncomingOrders(queue_size)
        {}

        void StartGateway(void)
        {
            std::thread riskThread(&RiskService<BatchSize>::startLoop, &Risk);
            std::thread serverThread(&EpollServer<BatchSize>::startServer,
                                     &Server);

            setAffinity(riskThread, 2); // Hard coded for local tests
            setAffinity(serverThread, 4);

            pthread_setname_np(riskThread.native_handle(), "RiskEngine");
            pthread_setname_np(serverThread.native_handle(), "EpollServer");

            riskThread.join();
            serverThread.join();
        }
    };
} // namespace Gateways
