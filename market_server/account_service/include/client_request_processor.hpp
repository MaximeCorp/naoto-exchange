#pragma once

#include <client_account_snapshot.hpp>
#include <client_states.hpp>
#include <routed_auth_request.hpp>
#include <gateway_response_dispatcher.hpp>
#include <gtest/gtest_prod.h>
#include <routed_message.hpp>
#include <object_batch.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>
#include <system_conf.hpp>

namespace naoto::account_service
{

    class ClientRequestProcessor
    {
        using MessageQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize> *>;
        using ResponsesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            RoutedMessage<ClientAccountSnapshot<MaxPositions>>>;

    private:
        ClientStates<MaxPositions> &States;
        MessageQueue &IncomingMessages;
        ResponsesQueue &ResponsesSend;
        StoragePool<
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize>>
            &MessagesPool;
        StoragePool<ClientAccountSnapshot<MaxPositions>> &ResponsesPool;

        // Client connect/disconnect + auth handling only - runs once per
        // client (dis)connection, not on the per-order hot path. Defined
        // out of line in client_request_processor.cpp so the SHA/openssl
        // and iostream machinery it uses doesn't have to be reparsed by
        // every translation unit that includes this header.
        void ProcessMessage(void);

        // StartLoop() is the only public entry point and it's an
        // infinite loop, so there's no way to drive a single
        // connect/disconnect/auth cycle synchronously without direct
        // access to ProcessMessage(). See
        // tests/market_server/unit/test_client_request_processor.cpp.
        // ProcessMessage() is private, and most test bodies drive it
        // through the SubmitOne() helper below rather than repeating
        // the enqueue+call boilerplate - so, same as EpollServerTest,
        // the fixture class itself needs a plain friend declaration in
        // addition to the individual FRIEND_TEST entries (friendship
        // isn't inherited down to the TEST_F-generated subclasses'
        // helper-method calls).
        friend class ClientRequestProcessorTest;
        FRIEND_TEST(ClientRequestProcessorTest, AcceptsValidConnectionRequest);
        FRIEND_TEST(ClientRequestProcessorTest, RejectsWrongCredentials);
        FRIEND_TEST(ClientRequestProcessorTest, RejectsUnauthorizedGateway);
        FRIEND_TEST(ClientRequestProcessorTest, AllowsGatewayIdTenRegardlessOfAuthorization);
        FRIEND_TEST(ClientRequestProcessorTest, UnrecognizedRequestTypeIsIgnoredWithoutCrashing);
        FRIEND_TEST(ClientRequestProcessorTest, BatchOfMultipleRequestsAllProcessed);

    public:
        ClientRequestProcessor(
            ClientStates<MaxPositions> &states, MessageQueue &incomingMessages,
            ResponsesQueue &responsesSend,
            StoragePool<ObjectBatch<RoutedAuthRequest,
                                   AccountEpollReceiveBatchSize>>
                &messagesPool,
            StoragePool<ClientAccountSnapshot<MaxPositions>> &responsesPool);

        void StartLoop(void);
    };
} // namespace naoto::account_service
