#pragma once

#include <ClientRequest.hpp>
#include <ClientRequestResponse.hpp>
#include <ClientStates.hpp>
#include <GatewayWriter.hpp>
#include <MessageContainer.hpp>
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
        using ResponsesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            MessageContainer<ClientRequestResponse<MaxPositions>>>;

    private:
        ClientStates<MaxPositions> &States;
        MessageQueue &IncomingMessages;
        ResponsesQueue &ResponsesSend;
        StoragePool<ObjectBatch<ClientRequest, BatchSize>> &MessagesPool;
        StoragePool<ClientRequestResponse<MaxPositions>> &ResponsesPool;
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

                    uint16_t gatewayId = curMessage.GatewayId;
                    uint32_t clientId = curMessage.ClientId;
                    std::array<uint8_t, 32> &key = curMessage.Key;

                    ClientState<MaxPositions> &curClient = States[clientId];

                    if (curMessage.RequestType == 'A')
                    {
                        if (curClient.GetAuthorized() != gatewayId
                            || curClient.GetConnected() != -1)
                        {
                            // Client connection denied
                            ClientRequestResponse<MaxPositions> *curResponse =
                                ResponsesPool.acquire();
                            curResponse->Clear();
                            curResponse->Status = 'R';
                            curResponse->ClientId = clientId;
                            curResponse->ClientFd = curMessage.ClientFd;
                            continue;
                        }

                        if (!curClient.CheckKey(key))
                        {
                            // Send refuse response: wrong credentials
                            ClientRequestResponse<MaxPositions> *curResponse =
                                ResponsesPool.acquire();
                            curResponse->Clear();
                            curResponse->Status = 'C';
                            curResponse->ClientId = clientId;
                            curResponse->ClientFd = curMessage.ClientFd;
                            continue;
                        }

                        // Send details
                        ClientRequestResponse<MaxPositions> *curResponse =
                            ResponsesPool.acquire();
                        curResponse->Status = 'A';
                        curResponse->SequenceId = 0; // Handle sequence ID later
                        curResponse->ClientId = clientId;
                        curResponse->ClientFd = curMessage.ClientFd;
                        curResponse->AssetId = curClient.AssetId;
                        curResponse->Confirmed = curClient.Confirmed;
                        curResponse->Attempt = curClient.Attempt;

                        MessageContainer<ClientRequestResponse<MaxPositions>>
                            toPush;

                        toPush.GatewayId = gatewayId;
                        toPush.Message = curResponse;

                        ResponsesSend.wait_enqueue(toPush);
                    }
                    else if (curMessage.RequestType == 'D')
                    {
                        curClient.SetConnected(-1);
                        // Might have to send ACK to gateways
                    }
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
