#pragma once

#include <ClientRequest.hpp>
#include <ClientStates.hpp>
#include <ClientStatesKeeper.hpp>
#include <EpollServer.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>

namespace ClientDetailsProvider
{
    template <size_t BatchSize, size_t MaxPositions>
    class ClientDetailsProvider
    {
        using RequestQueue = BlockingReaderWriterCircularBuffer<
            ObjectBatch<ClientRequest, BatchSize> *>;

    private:
        EpollServer<ClientRequest, BatchSize> Server;
        ClientStates<MaxPosisitions> States;
        ClientStatesKeeper<MaxPositions> Keeper;
        StoragePool<ObjectBatch<ClientRequest, BatchSize>> RequestPool;
        RequestQueue Requests;

    public:
        ClientDetailsProvider<BatchSize, MaxPositions>()
        {}
    };
} // namespace ClientDetailsProvider
