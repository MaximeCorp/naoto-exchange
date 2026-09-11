#pragma once

#include <consumer.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <versioned_fd.hpp>

namespace naoto::order_gateway
{
    class GatewayRequestForwarder
        : public Consumer<GatewayRequestForwarder, RoutedAuthRequest,
                          GatewayRequestQueueSize, GatewayRequestPoolSize>
    {
        using Base = Consumer<GatewayRequestForwarder, RoutedAuthRequest,
                              GatewayRequestQueueSize, GatewayRequestPoolSize>;
        using RequestQueue = SpscQueue<RoutedAuthRequest *, GatewayMaxClients>;

    private:
        VersionedFd &AccountFd;

        void SendRequest(RoutedAuthRequest *curRequest) noexcept;

    public:
        GatewayRequestForwarder(
            RequestQueue *requestsQueue,
            StoragePool<RoutedAuthRequest, GatewayRequestPoolSize>
                &requestsPool,
            VersionedFd &accountFd)
            : Base(requestsQueue, requestsPool)
            , AccountFd(accountFd)
        {}

        void Handle(RoutedAuthRequest *curRequest) noexcept;
    };

    class AccountRequestSender
    {
        using RequestQueue = SpscQueue<RoutedAuthRequest *, GatewayMaxClients>;

    private:
        GatewayRequestForwarder EpollConsumer;
        GatewayRequestForwarder StatesWriterConsumer;

    public:
        AccountRequestSender(
            RequestQueue *epollRequests, RequestQueue *statesWriterRequests,
            StoragePool<RoutedAuthRequest, GatewayRequestPoolSize> &epollPool,
            StoragePool<RoutedAuthRequest, GatewayRequestPoolSize>
                &statesWriterPool,
            VersionedFd &accountFd)
            : EpollConsumer(epollRequests, epollPool, accountFd)
            , StatesWriterConsumer(statesWriterRequests, statesWriterPool,
                                   accountFd)
        {}

        // Startup-only entry point for this service's forwarding loop -
        // defined out of line in account_request_sender.cpp.
        void StartLoop(void) noexcept;
    };

} // namespace naoto::order_gateway
