#pragma once

#include <order_gateway_types.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>
#include <versioned_fd.hpp>

namespace naoto::order_gateway
{
    class GatewayRequestForwarder : public GatewayRequestForwarderBase
    {
    private:
        VersionedFd &AccountFd;

        void SendRequest(RoutedAuthRequest *curRequest) noexcept;

    public:
        GatewayRequestForwarder(AuthRequestQueue *requestsQueue,
                                AuthRequestMempool &requestsPool,
                                VersionedFd &accountFd)
            : GatewayRequestForwarderBase(requestsQueue, requestsPool)
            , AccountFd(accountFd)
        {}

        void Handle(RoutedAuthRequest *curRequest) noexcept;
    };

    class AccountRequestSender
    {
    private:
        GatewayRequestForwarder EpollConsumer;
        GatewayRequestForwarder StatesWriterConsumer;

    public:
        AccountRequestSender(
            AuthRequestQueue *epollRequests,
            AuthRequestQueue *statesWriterRequests,
            AuthRequestMempool &epollPool,
            AuthRequestMempool &statesWriterPool, VersionedFd &accountFd)
            : EpollConsumer(epollRequests, epollPool, accountFd)
            , StatesWriterConsumer(statesWriterRequests, statesWriterPool,
                                   accountFd)
        {}

        // Startup-only entry point for this service's forwarding loop -
        // defined out of line in account_request_sender.cpp.
        void StartLoop(void) noexcept;
    };

} // namespace naoto::order_gateway
