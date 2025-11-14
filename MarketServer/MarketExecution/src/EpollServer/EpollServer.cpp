#include <EpollServer.hpp>

namespace MarketExecution
{
    EpollServer::EpollServer(int port, int maxEvents, int maxPending,
                             StoragePool<Order> *pool)
        : Port(port)
        , MaxEvents(maxEvents)
        , MaxPending(maxPending)
        , Buffers()
        , Pool(pool)
    {
        initSocket();
    }

    EpollServer::~EpollServer()
    {
        close(EpollFd);
        close(ListenFd);
    }

    void EpollServer::setNonBlocking(int fd)
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

    void EpollServer::initSocket()
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
        if (epoll_ctl(EpollFd, EPOLL_CTL_ADD, EpollFd, &event) == -1)
        {
            perror("epoll_ctl: listen_fd");
            exit(EXIT_FAILURE);
        }
    }

    void EpollServer::addClient(int clientFd)
    {
        setNonBlocking(clientFd);

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        event.data.fd = clientFd;
        if (epoll_ctl(EpollFd, EPOLL_CTL_ADD, clientFd, &event) == -1)
        {
            perror("epoll_ctl: client_fd add");
            close(clientFd);
        }

        Buffers[clientFd] = "";
    }

    void EpollServer::removeClient(int clientId)
    {
        epoll_ctl(EpollFd, EPOLL_CTL_DEL, clientId, nullptr);

        close(clientId);

        std::cout << "Closed connection on FD: " << clientId << std::endl;
    }

    void EpollServer::addOrder(std::string &buffer,
                               boost::lockfree::queue<Order *> &OrdersQueue)
    {
        size_t start = 0;

        while (start + sizeof(Order) <= buffer.size())
        {
            Order *toAdd = Pool->acquire();
            parseBinOrder(buffer.c_str() + start, sizeof(Order), toAdd);

            OrdersQueue.push(toAdd);

            start += sizeof(Order);
        }

        buffer.erase(0, start);
    }

    void EpollServer::startServer(boost::lockfree::queue<Order *> &OrdersQueue)
    {
        std::vector<struct epoll_event> events(MaxEvents);
        while (true)
        {
            int nfds = epoll_wait(EpollFd, events.data(), MaxEvents, -1);

            if (nfds == -1)
            {
                if (errno == EINTR)
                    continue;
                perror("epoll_wait");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i < nfds; ++i)
            {
                int curFd = events[i].data.fd;

                if (curFd == ListenFd)
                {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int clientFd;

                    while ((clientFd = accept(ListenFd,
                                              (struct sockaddr *)&client_addr,
                                              &client_len))
                           != -1)
                    {
                        addClient(clientFd);
                    }

                    if (clientFd == -1 && errno != EAGAIN
                        && errno != EWOULDBLOCK)
                    {
                        perror("error while accepting");
                    }
                }
                else
                {
                    if (events[i].events
                        & (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP))
                    {
                        if (events[i].events
                            & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
                        {
                            removeClient(curFd);
                        }

                        char buffer[1024];
                        ssize_t nread;

                        while ((nread = read(curFd, buffer, sizeof(buffer)))
                               > 0)
                        {
                            Buffers[curFd].append(buffer, nread);
                        }

                        if (Buffers[curFd].size())
                        {
                            addOrder(Buffers[curFd], OrdersQueue);
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
            }
        }
    }

    void startSocketLoop(EpollServer &server,
                         boost::lockfree::queue<Order *> &OrdersQueue)
    {
        server.startServer(OrdersQueue);
    }

} // namespace MarketExecution
