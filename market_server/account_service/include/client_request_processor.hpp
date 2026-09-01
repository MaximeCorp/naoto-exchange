#pragma once

#include <client_account_snapshot.hpp>
#include <client_states.hpp>
#include <routed_auth_request.hpp>
#include <gateway_response_dispatcher.hpp>
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
