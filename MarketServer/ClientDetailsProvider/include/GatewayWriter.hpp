#pragma once

#include <ClientRequestResponse.hpp>
#include <ClientUpdate.hpp>
#include <FdGen.hpp>
#include <MessageContainer.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstring>
#include <sys/socket.h>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions, size_t BatchesSize, size_t MaxGateways>
    class GatewayWriter
    {
        // Might be worth batching
        using ResponsesQueue = BlockingReaderWriterCircularBuffer<
            MessageContainer<ClientRequestResponse<MaxPositions>>>;
        using UpdatesQueue =
            BlockingReaderWriterCircularBuffer<MessageContainer<ClientUpdate>>;

    private:
        ResponsesQueue &Responses;
        UpdatesQueue &Updates;
        StoragePool<ClientRequestResponse> &ResponsesPool;
        StoragePool<ClientUpdate> &UpdatesPool;
        std::array<FdGen, MaxGateways> &GatewayFd; // Consumer
        std::array<uint8_t,
                   max(sizeof(ClientRequestResponse), sizeof(ClientUpdate))
                       * BatchesSize>
            Buffer;
        size_t BufferSize;

        void ConsumeMessages(void) noexcept
        {
            for (size_t i = 0; i < BatchesSize; ++i)
            {
                const MessageContainer<ClientRequestResponse> curResponse;

                if (!Responses.try_dequeue(curResponse)) [[unlikely]]
                {
                    break;
                }

                std::memcpy(curResponse.Message, Buffer.data() + BufferSize,
                            sizeof(ClientRequestResponse));
                BufferSize += sizeof(ClientRequestResponse);
            }

            if (BufferSize) [[likely]]
            {
                FdGen &gatewayFd = GatewayFd[curResponse.GatewayId];

                int32_t fd =
                    FdGen::Fd(gatewayFd.load(std::memory_order_acquire));

                send(fd, Buffer, BufferSize, 0);
            }

            BufferSize = 0;

            for (size_t i = 0; i < BatchesSize; ++i)
            {
                const MessageContainer<ClientUpdate> curUpdate;

                if (!Updates.try_dequeue(curUpdate)) [[unlikely]]
                {
                    break;
                }

                std::memcpy(curUpdate.Message, Buffer.data() + BufferSize,
                            sizeof(ClientUpdate));
                BufferSize += sizeof(ClientUpdate);
            }

            if (BufferSize) [[likely]]
            {
                FdGen &gatewayFd = GatewayFd[curResponse.GatewayId];

                int32_t fd =
                    FdGen::Fd(gatewayFd.load(std::memory_order_acquire));

                send(fd, Buffer, BufferSize, 0);
            }

            BufferSize = 0;
        }

    public:
        GatewayWriter(ResponsesQueue &responses, UpdatesQueue &updates,
                      StoragePool<ClientRequestResponse> &responsesPool,
                      StoragePool<ClientUpdate> &updatesPool)
            : Responses(responses)
            , Updates(updates)
            , ResponsesPool(responsesPool)
            , UpdatesPool(updatesPool)
            , BufferSize(0)
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {}
        }
    };
} // namespace ClientDetailsProvider
