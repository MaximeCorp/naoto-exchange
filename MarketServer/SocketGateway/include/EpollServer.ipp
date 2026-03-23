#pragma once

#include <EpollServer.hpp>

namespace Gateways
{
    template <size_t BatchSize>
    void EpollServer<BatchSize>::initSocket()
    {
        struct sockaddr_in server_addr;

        if ((ListenFd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        setNonBlocking(ListenFd);

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(Port);

        int opt = 1;
        setsockopt(ListenFd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                   sizeof(opt));

        if (bind(ListenFd, (struct sockaddr *)&server_addr, sizeof(server_addr))
            == -1)
        {
            perror("bind");
            exit(EXIT_FAILURE);
        }
        if (listen(ListenFd, MaxPending) == -1)
        {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        EpollFd = epoll_create1(0);
        if (EpollFd == -1)
        {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = ListenFd;
        if (epoll_ctl(EpollFd, EPOLL_CTL_ADD, ListenFd, &event) == -1)
        {
            perror("epoll_ctl: listen_fd");
            exit(EXIT_FAILURE);
        }
    }
} // namespace Gateways
