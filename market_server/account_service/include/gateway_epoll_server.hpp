#pragma once

#include <account_service_types.hpp>
#include <cstddef>
#include <file_descriptors_ops.hpp>
#include <gateway_handshake.hpp>
#include <object_batch.hpp>
#include <routed_auth_request.hpp>
#include <spsc_queue.hpp>
#include <system_conf.hpp>
#include <versioned_fd.hpp>

namespace naoto::account_service
{
    class GatewayEpollServer : public GatewayEpollServerBase
    {
    private:
        GatewayFds &Gateways;
        std::vector<uint16_t> FdToGateways;

    public:
        GatewayEpollServer(const int port, const int maxEvents,
                           const int maxPending, AuthRequestMempool &pool,
                           AuthRequestQueue *orders, GatewayFds &gateways)
            : GatewayEpollServerBase(port, maxEvents, maxPending, pool, orders)
            , Gateways(gateways)
            , FdToGateways(FileDescriptorsOps::getMaxFd(), 0)
        {}

        void FirstMessageHandle(GatewayHandshake *message, uint32_t fd)
        {
            if (message->GatewayId > MaxGateways) [[unlikely]]
            {
                std::cout << "invalid gateway id: " << message->GatewayId
                          << "\n\n";
                return;
            }

            std::cout << "Gateway " << message->GatewayId << " connected at fd "
                      << fd << ".\n\n";
            Gateways[message->GatewayId].SwitchFd(fd);
            FdToGateways[fd] = message->GatewayId;
        }

        void CloseHandle(uint32_t fd) noexcept
        {
            std::cout << "Connection to gateway " << FdToGateways[fd]
                      << " was closed.\n\n";
            Gateways[FdToGateways[fd]].SwitchFd(-1);
        }
    };
} // namespace naoto::account_service