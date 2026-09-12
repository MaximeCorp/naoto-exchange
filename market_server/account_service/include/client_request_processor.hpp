#pragma once

#include <account_service_types.hpp>
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
    private:
        ClientStates &States;
        AuthRequestConsumer IncomingMessages;
        AccountResponseProducer ResponsesSend;
        AuthRequestMempool &MessagesPool;
        AccountResponseMempool &ResponsesPool;

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
            ClientStates &states, AuthRequestQueue *incomingMessages,
            AccountResponseQueue *responsesSend,
            AuthRequestMempool &messagesPool,
            AccountResponseMempool &responsesPool);

        void StartLoop(void);
    };
} // namespace naoto::account_service
