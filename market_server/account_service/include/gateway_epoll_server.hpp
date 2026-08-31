#pragma once

#include <cstddef>
#include <epoll_server.hpp>
#include <versioned_fd.hpp>
#include <file_descriptors_ops.hpp>
#include <gateway_handshake.hpp>
#include <routed_auth_request.hpp>
#include <object_batch.hpp>
#include <readerwritercircularbuffer.h>

namespace naoto::account_service
{
    template <size_t BatchSize, size_t MaxGateways>
    class GatewayEpollServer
        : public EpollServer<
              GatewayEpollServer<BatchSize, MaxGateways>,
              RoutedAuthRequest, BatchSize, GatewayHandshake>
    {
        using Base = EpollServer<GatewayEpollServer, RoutedAuthRequest,
                                 BatchSize, GatewayHandshake>;
        using RequestQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<RoutedAuthRequest, BatchSize> *>;

    private:
        std::array<VersionedFd, MaxGateways> &Gateways;
        std::vector<uint16_t> FdToGateways;

    public:
        GatewayEpollServer(
            const int port, const int maxEvents, const int maxPending,
            StoragePool<ObjectBatch<RoutedAuthRequest, BatchSize>> &pool,
            RequestQueue &orders,
            std::array<VersionedFd, MaxGateways> &gateways)
            : Base(port, maxEvents, maxPending, pool, orders)
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