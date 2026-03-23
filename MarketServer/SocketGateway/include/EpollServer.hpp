#pragma once

#include <FileDescriptorsOps.hpp>
#include <Order.hpp>
#include <OrderBatch.hpp>
#include <OrderBuffer.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <atomic>
#include <cstdlib>
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
    template <size_t BatchSize>
    class EpollServer
    {
        using OrderQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        int ListenFd;
        int EpollFd;
        int ClientStatesUpdateFd;
        const int Port;
        const int MaxEvents;
        const int MaxPending;

        std::vector<OrderBuffer> Buffers;
        StoragePool<OrderBatch<BatchSize>> &Pool;
        alignas(64) OrderQueue &Orders;

        inline void setNonBlocking(const int fd) noexcept
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1)
            {
                perror("fcntl F_GETFL");
                exit(EXIT_FAILURE);
            }
            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
            {
                perror("fcntl F_SETFL O_NONBLOCK");
                exit(EXIT_FAILURE);
            }
        }

        inline void addClient(const int clientFd) noexcept
        {
            setNonBlocking(clientFd);

            struct epoll_event event;
            event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            event.data.fd = clientFd;
            if (epoll_ctl(EpollFd, EPOLL_CTL_ADD, clientFd, &event) == -1)
            {
                perror("epoll_ctl: client_fd add\n");
                close(clientFd);
            }

            Buffers[clientFd] = {};
        }

        inline void removeClient(const int clientFd) noexcept
        {
            epoll_ctl(EpollFd, EPOLL_CTL_DEL, clientFd, nullptr);

            close(clientFd);

            std::cout << "Closed connection on FD: " << clientFd << std::endl;
        }

        void readMessage(struct epoll_event &event, const int curFd) noexcept
        {
            if (event.events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
            {
                removeClient(curFd);
                return;
            }

            if (event.events & EPOLLIN)
            {
                OrderBatch<BatchSize> *batch = nullptr;
                OrderBuffer &buffer = Buffers[curFd];

                std::cout << "Content of buffer:\n";

                for (size_t i = 0; i < buffer.BufferSize; ++i)
                {
                    std::cout << buffer.Buffer[i];
                }

                std::cout << "\n";

                ssize_t nread;

                while (true)
                {
                    batch = Pool.acquire();

                    if (!batch) [[unlikely]]
                    {
                        // Send error message to client (don't forget to give
                        // context: which order was refused)
                        break;
                    }

                    if (buffer.BufferSize > 0) [[likely]]
                    {
                        std::memcpy(batch->Data.data(), buffer.Buffer.data(),
                                    buffer.BufferSize);
                    }

                    nread = read(
                        curFd, (char *)(batch->Data.data()) + buffer.BufferSize,
                        sizeof(Order) * BatchSize - buffer.BufferSize);

                    if (nread <= 0) [[unlikely]]
                    {
                        Pool.releaseCritical(batch);

                        batch = nullptr;
                        break;
                    }

                    batch->setFd(curFd);

                    auto [batchSize, bufferSize] = std::div(
                        (int)(nread + buffer.BufferSize), sizeof(Order));

                    batch->setSize(batchSize);

                    buffer.addBytes(
                        batch->Data.data() + sizeof(Order) * batchSize,
                        bufferSize); // Double check if sizeof(Order) *
                                     // batchSize is right

                    if (!Orders.try_enqueue(batch)) [[unlikely]]
                    {
                        Pool.releaseCritical(batch);
                        // Send error message to client
                    }

                    if (batchSize <= 0)
                    {
                        std::cout << "cleared\n";
                    }

                    buffer.BufferSize *= batchSize <= 0;

                    batch = nullptr;
                }

                if (batch != nullptr)
                {
                    Pool.releaseCritical(batch);
                }

                if (nread == 0)
                {
                    removeClient(curFd);
                }
                else if (nread == -1)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        perror("read error");
                        removeClient(curFd);
                    }
                }
            }
        }

        void clientAcceptLoop(const int curFd) noexcept
        {
            if (curFd == ListenFd)
            {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int clientFd;

                while ((clientFd =
                            accept(ListenFd, (struct sockaddr *)&client_addr,
                                   &client_len))
                       != -1)
                {
                    addClient(clientFd);
                }

                if (clientFd == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    perror("error while accepting");
                }
            }
        }

        void initSocket(void); // Not in hot path

    public:
        EpollServer(const int port, const int maxEvents, const int maxPending,
                    StoragePool<OrderBatch<BatchSize>> &pool,
                    OrderQueue &orders, const int clientStatesUpdateFd,
                    const size_t nb_fds)
            : ClientStatesUpdateFd(clientStatesUpdateFd)
            , Port(port)
            , MaxEvents(maxEvents)
            , MaxPending(maxPending)
            , Pool(pool)
            , Orders(orders)
        {
            Buffers.resize(nb_fds);
            initSocket();
        }

        EpollServer(const int port, const int maxEvents, const int maxPending,
                    StoragePool<OrderBatch<BatchSize>> &pool,
                    const int clientStatesUpdateFd, OrderQueue &orders)
            : ClientStatesUpdateFd(clientStatesUpdateFd)
            , Port(port)
            , MaxEvents(maxEvents)
            , MaxPending(maxPending)
            , Pool(pool)
            , Orders(orders)
        {
            Buffers.resize(FileDescriptorsOps::findMaxFd());
            initSocket();
        }

        void startServer(void) noexcept
        {
            std::vector<struct epoll_event> events(MaxEvents);

            while (true)
            {
                int nfds = epoll_wait(EpollFd, events.data(), MaxEvents, -1);

                if (nfds == -1)
                {
                    if (errno == EINTR)
                        continue;
                    perror("epoll_wait failed");
                    exit(EXIT_FAILURE);
                }

                for (int i = 0; i < nfds; ++i)
                {
                    int curFd = events[i].data.fd;

                    if (curFd == ListenFd)
                    {
                        clientAcceptLoop(curFd);
                    }
                    else
                    {
                        readMessage(events[i], curFd);
                    }
                }
            }
        }

        ~EpollServer()
        {
            close(EpollFd);
            close(ListenFd);
        }
    };

} // namespace Gateways

#include "EpollServer.ipp"
