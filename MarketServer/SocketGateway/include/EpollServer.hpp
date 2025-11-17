#pragma once

#include <Order.hpp>
#include <StoragePool.hpp>
#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace Gateways
{
    class EpollServer
    {
    private:
        int ListenFd;
        int EpollFd;
        int Port;
        int MaxEvents;
        int MaxPending;

        std::unordered_map<int, std::string> Buffers;
        StoragePool<Order> *Pool;

        void setNonBlocking(int fd);
        void initSocket();
        void addClient(int clientFd);
        void removeClient(int clientFd);

    public:
        EpollServer(int port, int maxEvents, int maxPending,
                    StoragePool<Order> *pool);
        ~EpollServer();

        void startServer();
    };

} // namespace Gateways
