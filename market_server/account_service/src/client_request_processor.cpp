#include <client_request_processor.hpp>
#include <iostream>
#include <openssl/sha.h>

namespace naoto::account_service
{
    void ClientRequestProcessor::ProcessMessage(void)
    {
        ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize> *curBatch =
            nullptr;

        if (IncomingMessages.try_dequeue(curBatch)) [[likely]]
        {
            std::cout << "Received messages batch of size "
                      << curBatch->getSize() << " at client states keeper\n";

            for (size_t i = 0; i < curBatch->getSize(); ++i)
            {
                const RoutedAuthRequest &curMessage = (*curBatch)[i];

                // Might do that later to avoid unnecessary memory accesses
                uint16_t gatewayId = curMessage.GatewayId;
                uint32_t clientId = curMessage.ClientId;
                const std::array<uint8_t, 32> &key = curMessage.Key;

                ClientState<MaxPositions> curClient =
                    States.GetClientState(clientId);

                if (curMessage.RequestType == 'A')
                {
                    std::cout << "Received a client connection request from "
                                 "gateway "
                              << curMessage.GatewayId
                              << "\nRequest details:\n- client id: "
                              << curMessage.ClientId
                              << "\n- gateway fd: " << curBatch->getFd()
                              << "\n\n";

                    // TODO : Remove the hardcoded gateway

                    if ((curClient.GetAuthorized() != gatewayId
                         || curClient.GetConnected() != -1)
                        && gatewayId != 10)
                    {
                        // Client connection denied
                        ClientAccountSnapshot<MaxPositions> *curResponse =
                            ResponsesPool.acquire();
                        curResponse->Clear();
                        curResponse->Status = 'R';
                        curResponse->ClientId = clientId;
                        curResponse->ClientFd = curMessage.ClientFd;

                        RoutedMessage<ClientAccountSnapshot<MaxPositions>>
                            toPush;

                        toPush.GatewayId = gatewayId;
                        toPush.Message = curResponse;

                        ResponsesSend.wait_enqueue(toPush);

                        std::cout << "Refused: unauthorized gateway\n\n";

                        continue;
                    }

                    if (!curClient.CheckKey(key))
                    {
                        // Send refuse response: wrong credentials
                        ClientAccountSnapshot<MaxPositions> *curResponse =
                            ResponsesPool.acquire();
                        curResponse->Clear();
                        curResponse->Status = 'C';
                        curResponse->ClientId = clientId;
                        curResponse->ClientFd = curMessage.ClientFd;

                        RoutedMessage<ClientAccountSnapshot<MaxPositions>>
                            toPush;

                        toPush.GatewayId = gatewayId;
                        toPush.Message = curResponse;

                        ResponsesSend.wait_enqueue(toPush);

                        std::cout << "Refused: wrong credentials\n\n";

                        continue;
                    }

                    // Send details
                    ClientAccountSnapshot<MaxPositions> *curResponse =
                        ResponsesPool.acquire();
                    curResponse->Status = 'A';
                    curResponse->SequenceId = 0; // Handle sequence ID later
                    curResponse->ClientId = clientId;
                    curResponse->ClientFd = curMessage.ClientFd;
                    curResponse->AssetId = curClient.AssetId;
                    curResponse->Confirmed = curClient.Confirmed;
                    curResponse->Attempt = curClient.Attempt;

                    RoutedMessage<ClientAccountSnapshot<MaxPositions>> toPush;

                    toPush.GatewayId = gatewayId;
                    toPush.Message = curResponse;

                    std::cout << "Accepted\n\n";

                    ResponsesSend.wait_enqueue(toPush);
                }
                else if (curMessage.RequestType == 'D')
                {
                    // Might have to send ACK to gateways
                    // TODO: Connection requests should also contain if the
                    // client's already connected to avoid having to send
                    // acks
                }
                else
                {
                    // Handle invalid request
                }
            }

            if (!MessagesPool.release(curBatch)) [[unlikely]]
            {
                std::cerr << "Failed releasing to messages pool in process "
                             "message\n";
                std::terminate();
            }
        }
    }

    ClientRequestProcessor::ClientRequestProcessor(
        ClientStates<MaxPositions> &states, MessageQueue &incomingMessages,
        ResponsesQueue &responsesSend,
        StoragePool<ObjectBatch<RoutedAuthRequest,
                                AccountEpollReceiveBatchSize>> &messagesPool,
        StoragePool<ClientAccountSnapshot<MaxPositions>> &responsesPool)
        : States(states)
        , IncomingMessages(incomingMessages)
        , ResponsesSend(responsesSend)
        , MessagesPool(messagesPool)
        , ResponsesPool(responsesPool)
    {}

    void ClientRequestProcessor::StartLoop(void)
    {
        while (true)
        {
            ProcessMessage();
        }
    }
} // namespace naoto::account_service
