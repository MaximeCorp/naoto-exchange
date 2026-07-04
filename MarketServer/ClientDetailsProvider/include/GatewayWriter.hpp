#pragma once

#include <ClientRequestResponse.hpp>
#include <ClientUpdate.hpp>
#include <FdGen.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions>
    class GatewayWriter
    {
        // Might be worth batching
        using ResponsesQueue = BlockingReaderWriterCircularBuffer<
            ClientRequestResponse<MaxPositions> *>;
        using UpdatesQueue = BlockingReaderWriterCircularBuffer<ClientUpdate *>;

    private:
        ResponsesQueue &Responses;
        UpdatesQueue &Updates;
        StoragePool<ClientRequestResponse> &ResponsesPool;
        StoragePool<ClientUpdate> &UpdatesPool;
        std::array<FdGen, MaxAsset> &GatewayFd; // Consumer
    public:
        GatewayWriter(ResponsesQueue &responses, UpdatesQueue &updates,
                      StoragePool<ClientRequestResponse> &responsesPool,
                      StoragePool<ClientUpdate> &updatesPool)
            : Responses(responses)
            , Updates(updates)
            , ResponsesPool(responsesPool)
            , UpdatesPool(updatesPool)
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {}
        }
    };
} // namespace ClientDetailsProvider
