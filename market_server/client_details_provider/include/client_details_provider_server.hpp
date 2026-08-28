#pragma once

#include <cstddef>
#include <epoll_server.hpp>
#include <fd_gen.hpp>
#include <file_descriptors_ops.hpp>
#include <gateway_connection.hpp>
#include <gateway_request.hpp>
#include <object_batch.hpp>
#include <readerwritercircularbuffer.h>

namespace naoto::client_details_provider
{
    template <size_t BatchSize, size_t MaxGateways>
    class ClientDetailsProviderServer
        : public EpollServer<
              ClientDetailsProviderServer<BatchSize, MaxGateways>,
              GatewayRequest, BatchSize, GatewayConnection>
    {
        using Base = EpollServer<ClientDetailsProviderServer, GatewayRequest,
                                 BatchSize, GatewayConnection>;
        using RequestQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<GatewayRequest, BatchSize> *>;

    private:
        std::array<FdGen, MaxGateways> &Gateways;
        std::vector<uint16_t> FdToGateways;

    public:
        ClientDetailsProviderServer(
            const int port, const int maxEvents, const int maxPending,
            StoragePool<ObjectBatch<GatewayRequest, BatchSize>> &pool,
            RequestQueue &orders, std::array<FdGen, MaxGateways> &gateways)
            : Base(port, maxEvents, maxPending, pool, orders)
            , Gateways(gateways)
            , FdToGateways(FileDescriptorsOps::getMaxFd(), 0)
        {}

        void FirstMessageHandle(GatewayConnection *message, uint32_t fd)
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
} // namespace naoto::client_details_provider