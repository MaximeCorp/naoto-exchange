#pragma once

#include <atomic>
#include <concepts>
#include <cstdlib>
#include <fcntl.h>
#include <file_descriptors_ops.hpp>
#include <gtest/gtest_prod.h>
#include <iostream>
#include <netinet/in.h>
#include <object_batch.hpp>
#include <object_buffer.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace naoto
{
    template <typename DerivedServer>
    concept HasAcceptHandle = requires(DerivedServer server, uint32_t fd) {
        { server.AcceptHandle(fd) } -> std::same_as<void>;
    };

    template <typename DerivedServer>
    concept HasCloseHandle = requires(DerivedServer server, uint32_t fd) {
        { server.CloseHandle(fd) } -> std::same_as<void>;
    };

    template <typename DerivedServer, typename InitMessage>
    concept HasFirstMessageHandle =
        requires(DerivedServer server, InitMessage *message, uint32_t fd) {
            { server.FirstMessageHandle(message, fd) } -> std::same_as<void>;
        };

    template <typename DerivedServer, typename T, size_t BatchSize>
    concept HasBatchHandleServer = requires(
        DerivedServer server, ObjectBatch<T, BatchSize> *t, uint32_t fd) {
        { server.BatchHandle(t, fd) } -> std::same_as<void>;
    };

    template <typename DerivedServer, typename T, size_t BatchSize,
              size_t QueueSize, size_t PoolSize,
              typename InitMessage = std::monostate>
    class EpollServer
    {
        using ObjectQueue =
            SpscQueueProducer<ObjectBatch<T, BatchSize> *, QueueSize>;
        using FirstMessageBuffer =
            std::conditional_t<!std::is_same_v<std::monostate, InitMessage>,
                               std::vector<bool>, std::monostate>;

        static_assert(sizeof(T) >= sizeof(InitMessage),
                      "Epoll server: The first message can't be contained "
                      "because it's bigger than normal messages\n");

    private:
        int ListenFd;
        int EpollFd;
        int ClientStatesUpdateFd;
        const int Port;
        const int MaxEvents;
        const int MaxPending;

        std::vector<ObjectBuffer<T>> Buffers;
        [[no_unique_address]] FirstMessageBuffer FirstMessage;
        StoragePool<ObjectBatch<T, BatchSize>, PoolSize> &Pool;
        ObjectQueue OutgoingBatches;

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

            if constexpr (HasFirstMessageHandle<DerivedServer, InitMessage>)
            {
                std::cout
                    << "New connection, setting first message to true\n\n";
                FirstMessage[clientFd] = true;
            }

            if constexpr (HasAcceptHandle<DerivedServer>)
            {
                static_cast<DerivedServer *>(this)->AcceptHandle(clientFd);
            }

            Buffers[clientFd] = {};
        }

        inline void removeClient(const int clientFd) noexcept
        {
            if constexpr (HasCloseHandle<DerivedServer>)
            {
                static_cast<DerivedServer *>(this)->CloseHandle(clientFd);
            }

            epoll_ctl(EpollFd, EPOLL_CTL_DEL, clientFd, nullptr);

            close(clientFd);

            Buffers[clientFd].clearBuffer();

            std::cout << "Closed connection on FD: " << clientFd << "\n";
        }

        void readMessage(const uint32_t curFd) noexcept
        {
            ssize_t nread = -1;
            ObjectBuffer<T> &buffer = Buffers[curFd];

            if constexpr (HasFirstMessageHandle<DerivedServer, InitMessage>)
            {
                if (FirstMessage[curFd]) [[unlikely]]
                {
                    InitMessage firstMessage;
                    while (buffer.BufferSize < sizeof(InitMessage))
                    {
                        nread =
                            recv(curFd, &firstMessage,
                                 sizeof(InitMessage) - buffer.BufferSize, 0);

                        if (nread > 0)
                        {
                            if (!buffer.addBytes((T *)&firstMessage, nread))
                                [[unlikely]]
                            {
                                removeClient(curFd);
                                return;
                            }
                        }
                        else if (nread == 0)
                        {
                            removeClient(curFd);
                            return;
                        }
                        else
                        {
                            if (errno != EAGAIN && errno != EWOULDBLOCK
                                && errno != EINTR)
                            {
                                removeClient(curFd);
                                return;
                            }
                            break;
                        }
                    }

                    if (buffer.BufferSize >= sizeof(InitMessage))
                    {
                        buffer.readBytes(&firstMessage, sizeof(InitMessage));
                        static_cast<DerivedServer *>(this)->FirstMessageHandle(
                            &firstMessage, curFd);
                        FirstMessage[curFd] = false;
                    }
                    else
                    {
                        // No normal messages processing before full first
                        // message
                        return;
                    }
                }
            }

            ObjectBatch<T, BatchSize> *batch = nullptr;

            while (true)
            {
                batch = Pool.Acquire();

                if (!batch) [[unlikely]]
                {
                    // TODO: Handle drained pool
                    // Send error message to client (don't forget to give
                    // context: which item was refused)
                    break;
                }

                if (buffer.BufferSize > 0) [[likely]]
                {
                    std::memcpy(batch->Data.data(), buffer.Buffer.data(),
                                buffer.BufferSize);
                }

                nread = recv(curFd,
                             (char *)(batch->Data.data()) + buffer.BufferSize,
                             sizeof(T) * BatchSize - buffer.BufferSize, 0);

                if (nread <= 0) [[unlikely]]
                {
                    if (Pool.LocalRelease(batch)) [[unlikely]]
                    {
                        // TODO : handle this
                    }

                    if (errno == EINTR)
                    {
                        continue;
                    }

                    break;
                }

                batch->Fd = curFd;

                auto [batchSize, bufferSize] =
                    std::div((int)(nread + buffer.BufferSize), (int)sizeof(T));

                batch->Size = batchSize;

                buffer.clearBuffer();

                if (!buffer.addBytes(batch->Data.data() + sizeof(T) * batchSize,
                                     bufferSize)) [[unlikely]]
                {
                    // TODO : see if there's something to do here or if it can
                    // happen
                }

                if (batchSize > 0) [[likely]]
                {
                    if constexpr (HasBatchHandleServer<DerivedServer, T,
                                                       BatchSize>)
                    {
                        static_cast<DerivedServer *>(this)->BatchHandle(batch,
                                                                        curFd);
                    }

                    if (!OutgoingBatches.TryPush(batch)) [[unlikely]]
                    {
                        // TODO : think about what to do in this case
                        if (!Pool.LocalRelease(batch)) [[unlikely]]
                        {
                            // TODO : handle this
                        }
                    }
                }
                else
                {
                    if (!Pool.LocalRelease(batch)) [[unlikely]]
                    {
                        // TODO : handle this
                    }
                }
            }

            // TODO: Think about if it's necessary to free acquired batches

            if (nread == 0)
            {
                removeClient(curFd);
            }
            else if (nread < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                {
                    removeClient(curFd);
                    return;
                }
            }
        }

        void readEvent(struct epoll_event &event, const int curFd) noexcept
        {
            if (event.events & EPOLLIN)
            {
                readMessage(curFd);
                return;
            }

            if (event.events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
            {
                removeClient(curFd);
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

        void initSocket(void) noexcept; // Not in hot path

        // Testing hooks: EpollServer's only public API is startServer()
        // (an infinite epoll_wait() loop) and the destructor, so there's
        // no way to drive a single accept/read/close cycle synchronously
        // without these. tests/market_server/unit/test_epoll_server.cpp's
        // fixture puts most private-member access in shared helper methods
        // (ConnectClient(), GetBoundPort()) rather than repeating it in
        // every TEST_F body - friendship isn't inherited by the
        // TEST_F-generated subclasses down to those helpers, so the
        // fixture class itself needs a plain friend declaration too, not
        // just FRIEND_TEST per test case.
        friend class EpollServerTest;
        FRIEND_TEST(EpollServerTest, AddClientAddsToEpollAndFiresAcceptHandle);
        FRIEND_TEST(EpollServerTest,
                    FullMessageProducesOneBatchWithCorrectContent);
        FRIEND_TEST(EpollServerTest, MultipleMessagesInOneReadProduceOneBatch);
        FRIEND_TEST(EpollServerTest, PartialMessageDoesNotEnqueueAnEmptyBatch);
        FRIEND_TEST(EpollServerTest,
                    OrderlyCloseFiresRemoveClientAndCloseHandle);
        FRIEND_TEST(EpollServerTest, ReadEventDispatchesToRemoveClientOnHup);
        FRIEND_TEST(EpollServerTest, BatchHandleHookFiresBeforeEnqueue);

    public:
        EpollServer(const int port, const int maxEvents, const int maxPending,
                    StoragePool<ObjectBatch<T, BatchSize>, PoolSize> &pool,
                    ObjectQueue &outgoingBatches, const size_t nb_fds)
            : Port(port)
            , MaxEvents(maxEvents)
            , MaxPending(maxPending)
            , Pool(pool)
            , OutgoingBatches(outgoingBatches)
        {
            Buffers.resize(nb_fds);
            if constexpr (HasFirstMessageHandle<DerivedServer, InitMessage>)
            {
                FirstMessage.resize(nb_fds);
            }

            initSocket();
        }

        EpollServer(
            const int port, const int maxEvents, const int maxPending,
            StoragePool<ObjectBatch<T, BatchSize>, PoolSize> &pool,
            SpscQueue<ObjectBatch<T, BatchSize> *, QueueSize> *outgoingBatches)
            : Port(port)
            , MaxEvents(maxEvents)
            , MaxPending(maxPending)
            , Pool(pool)
            , OutgoingBatches(outgoingBatches)
        {
            Buffers.resize(FileDescriptorsOps::getMaxFd());

            if constexpr (HasFirstMessageHandle<DerivedServer, InitMessage>)
            {
                FirstMessage.resize(FileDescriptorsOps::getMaxFd());
            }

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
                        readEvent(events[i], curFd);
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

} // namespace naoto

#include "epoll_server.ipp"
