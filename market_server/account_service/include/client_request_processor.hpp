#pragma once

#include <client_account_snapshot.hpp>
#include <client_states.hpp>
#include <gateway_response_dispatcher.hpp>
#include <gtest/gtest_prod.h>
#include <object_batch.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <routed_message.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>

namespace naoto::account_service
{

    class ClientRequestProcessor
    {
        using MessageQueue = SpscQueueConsumer<
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize> *,
            MaxClients>;
        using ResponsesQueue = SpscQueueProducer<
            RoutedMessage<ClientAccountSnapshot<MaxPositions>>, MaxClients>;

    private:
        ClientStates<MaxPositions> &States;
        MessageQueue IncomingMessages;
        ResponsesQueue ResponsesSend;
        StoragePool<
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize>,
            AccountResponsePoolSize> &MessagesPool;
        StoragePool<ClientAccountSnapshot<MaxPositions>,
                    AccountResponsePoolSize> &ResponsesPool;

        void ProcessMessage(void);

        friend class ClientRequestProcessorTest;
        FRIEND_TEST(ClientRequestProcessorTest, AcceptsValidConnectionRequest);
        FRIEND_TEST(ClientRequestProcessorTest, RejectsWrongCredentials);
        FRIEND_TEST(ClientRequestProcessorTest, RejectsUnauthorizedGateway);
        FRIEND_TEST(ClientRequestProcessorTest,
                    AllowsGatewayIdTenRegardlessOfAuthorization);
        FRIEND_TEST(ClientRequestProcessorTest,
                    UnrecognizedRequestTypeIsIgnoredWithoutCrashing);
        FRIEND_TEST(ClientRequestProcessorTest,
                    BatchOfMultipleRequestsAllProcessed);

    public:
        ClientRequestProcessor(
            ClientStates<MaxPositions> &states,
            SpscQueue<
                ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize> *,
                MaxClients> *incomingMessages,
            SpscQueue<RoutedMessage<ClientAccountSnapshot<MaxPositions>>,
                      MaxClients> *responsesSend,
            StoragePool<
                ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize>,
                AccountResponsePoolSize> &messagesPool,
            StoragePool<ClientAccountSnapshot<MaxPositions>,
                        AccountResponsePoolSize> &responsesPool);

        void StartLoop(void);
    };
} // namespace naoto::account_service
