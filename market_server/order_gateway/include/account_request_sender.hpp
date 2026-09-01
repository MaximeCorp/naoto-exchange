#pragma once

#include <consumer.hpp>
#include <versioned_fd.hpp>
#include <routed_auth_request.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>

namespace naoto::order_gateway
{
    class GatewayRequestForwarder
        : public Consumer<GatewayRequestForwarder, RoutedAuthRequest>
    {
        using Base = Consumer<GatewayRequestForwarder, RoutedAuthRequest>;
        using RequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<RoutedAuthRequest *>;

    private:
        VersionedFd &AccountFd;

        // Not on the order hot path (this forwards auth/connect requests
        // to the account service, not orders) - defined out of line in
        // account_request_sender.cpp so <sys/socket.h> and the send()
        // logic aren't reparsed by every includer of this header.
        void SendRequest(RoutedAuthRequest *curRequest) noexcept;

    public:
        GatewayRequestForwarder(RequestQueue &requestsQueue,
                     StoragePool<RoutedAuthRequest> &requestsPool,
                     VersionedFd &accountFd)
            : Base(requestsQueue, requestsPool)
            , AccountFd(accountFd)
        {}

        void Handle(RoutedAuthRequest *curRequest) noexcept;
    };

    class AccountRequestSender
    {
        using RequestQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<RoutedAuthRequest *>;

    private:
        GatewayRequestForwarder EpollConsumer;
        GatewayRequestForwarder StatesWriterConsumer;

    public:
        AccountRequestSender(RequestQueue &epollRequests,
                         RequestQueue &statesWriterRequests,
                         StoragePool<RoutedAuthRequest> &epollPool,
                         StoragePool<RoutedAuthRequest> &statesWriterPool,
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
