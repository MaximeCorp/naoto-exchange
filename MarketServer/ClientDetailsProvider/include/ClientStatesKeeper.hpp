#pragma once

#include <ClientRequest.hpp>
#include <ClientStates.hpp>
#include <ObjectBatch.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <openssl/sha.h>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions, size_t BatchSize>
    class ClientStatesKeeper
    {
        using MessagesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<ClientRequest, BatchSize> *>;

    private:
        ClientStates<MaxPositions> &States;
        MessageQueue &IncomingMessages;
        StoragePool<ObjectBatch<ClientRequest, BatchSize>> &MessagesPool;
        std::vector<uint32_t> GatewayFd; // GatewayFd[n] for gateway number n

        void ProcessMessage(void)
        {
            ObjectBatch<ClientRequest, BatchSize> *curBatch = nullptr;

            if (IncomingMessages.try_dequeue(curBatch)) [[likely]]
            {
                std::cout << "Received messages batch of size "
                          << curBatch->getSize()
                          << " at client states keeper\n";

                for (size_t i = 0; i < curBatch->getSize(); ++i)
                {
                    const ClientRequest &curMessage = (*to_check)[i];

                    if (curMessage.RequestType == 'A')
                    {
                        uint16_t gatewayId = curMessage.GatewayId;
                        uint32_t clientId = curMessage.ClientId;
                        std::array<uint8_t, 32> &key = curMessage.Key;

                        ClientState<MaxPositions> &curClient = States[clientId];

                        if (curClient.GetAuthorized() != gatewayId
                            || curClient.GetConnected() != -1)
                        {
                            // Client connection denied
                            continue;
                        }

                        if (!curClient.CheckKey(key))
                        {
                            // Send refuse response
                            continue;
                        }

                        // Send details
                    }
                    else if (curMessage.RequestType == 'D')
                    {}
                    else
                    {
                        // Handle invalid request
                    }
                }

                MessagesPool.release(curBatch);
            }
        }

    public:
        ClientDetailsProvider(MessageQueue &incomingMessages)
            : IncomingMessages(incomingMessages)
        {}

        void StartLoop(void)
        {
            while (true)
            {
                ProcessMessage();
            }
        }
    };
} // namespace ClientDetailsProvider
