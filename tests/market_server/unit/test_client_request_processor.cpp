#include <gtest/gtest.h>
#include <client_request_processor.hpp>

#include <openssl/sha.h>

// Same reasoning as bid_ask.hpp / epoll_server.hpp: FRIEND_TEST binds to
// a fixture class in the exact namespace it was declared in
// (naoto::account_service), so the fixture and every TEST_F case have to
// live there directly, not in an anonymous namespace nested inside it.
namespace naoto::account_service
{
    namespace
    {
        std::array<uint8_t, 32> Sha256(const std::array<uint8_t, 32> &in)
        {
            std::array<uint8_t, 32> out{};
            SHA256(in.data(), in.size(), out.data());
            return out;
        }

        std::array<uint8_t, 32> MakeKey(uint8_t seed)
        {
            std::array<uint8_t, 32> key{};
            for (size_t i = 0; i < 32; ++i)
            {
                key[i] = static_cast<uint8_t>(seed + i);
            }
            return key;
        }
    } // namespace

    class ClientRequestProcessorTest : public ::testing::Test
    {
    protected:
        using MessageBatch =
            ObjectBatch<RoutedAuthRequest, AccountEpollReceiveBatchSize>;
        using ResponsesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            RoutedMessage<ClientAccountSnapshot<MaxPositions>>>;

        ClientStates<MaxPositions> states{16};
        moodycamel::BlockingReaderWriterCircularBuffer<MessageBatch *>
            incoming{16};
        ResponsesQueue responsesSend{16};
        StoragePool<MessageBatch> messagesPool{16};
        StoragePool<ClientAccountSnapshot<MaxPositions>> responsesPool{16};

        ClientRequestProcessor processor{states, incoming, responsesSend,
                                          messagesPool, responsesPool};

        void SeedClient(uint32_t clientId, int16_t authorizedGateway,
                         int16_t connected, std::array<uint8_t, 32> keyHash,
                         std::array<uint16_t, MaxPositions> assetIds = {},
                         std::array<int64_t, MaxPositions> confirmed = {})
        {
            ClientState<MaxPositions> s;
            s.ClientId = clientId;
            s.Key = keyHash;
            s.AssetId = assetIds;
            s.Confirmed = confirmed;
            s.Attempt = {};
            s.SetAuthorized(authorizedGateway);
            s.SetConnected(connected);
            states.SetClientState(s, 0);
            states.FlushTripleBuffer(clientId);
        }

        // Enqueues a single request as a one-element batch and runs
        // ProcessMessage() once (private, friend-granted access).
        void SubmitOne(RoutedAuthRequest request)
        {
            MessageBatch *batch = messagesPool.acquire();
            ASSERT_NE(batch, nullptr);
            batch->setSize(1);
            batch->setFd(request.ClientFd);
            (*batch)[0] = request;
            ASSERT_TRUE(incoming.try_enqueue(batch));
            processor.ProcessMessage();
        }

        RoutedMessage<ClientAccountSnapshot<MaxPositions>> DrainOneResponse()
        {
            RoutedMessage<ClientAccountSnapshot<MaxPositions>> msg;
            bool got = responsesSend.try_dequeue(msg);
            EXPECT_TRUE(got) << "expected a response to have been sent";
            return msg;
        }
    };
TEST_F(ClientRequestProcessorTest, AcceptsValidConnectionRequest)
{
    auto rawKey = MakeKey(1);
    SeedClient(/*clientId=*/1, /*authorizedGateway=*/99, /*connected=*/-1,
               Sha256(rawKey), {10, 20, 30}, {100, 200, 300});

    naoto::RoutedAuthRequest req{};
    req.RequestType = 'A';
    req.ClientId = 1;
    req.Key = rawKey;
    req.ClientFd = 42;
    req.GatewayId = 99;

    SubmitOne(req);

    auto response = DrainOneResponse();
    EXPECT_EQ(response.GatewayId, 99u);
    ASSERT_NE(response.Message, nullptr);
    EXPECT_EQ(response.Message->Status, 'A');
    EXPECT_EQ(response.Message->ClientId, 1u);
    EXPECT_EQ(response.Message->ClientFd, 42u);
    EXPECT_EQ(response.Message->Confirmed[1], 200);

    (void)responsesPool.release(response.Message);
}

TEST_F(ClientRequestProcessorTest, RejectsWrongCredentials)
{
    auto rawKey = MakeKey(2);
    SeedClient(2, 99, -1, Sha256(rawKey));

    naoto::RoutedAuthRequest req{};
    req.RequestType = 'A';
    req.ClientId = 2;
    req.Key = MakeKey(3); // wrong key
    req.ClientFd = 7;
    req.GatewayId = 99;

    SubmitOne(req);

    auto response = DrainOneResponse();
    EXPECT_EQ(response.Message->Status, 'C');
    EXPECT_EQ(response.Message->ClientId, 2u);
    (void)responsesPool.release(response.Message);
}

TEST_F(ClientRequestProcessorTest, RejectsUnauthorizedGateway)
{
    SeedClient(3, /*authorizedGateway=*/5, -1, MakeKey(4));

    naoto::RoutedAuthRequest req{};
    req.RequestType = 'A';
    req.ClientId = 3;
    req.Key = MakeKey(4); // irrelevant, rejected before key check
    req.ClientFd = 8;
    req.GatewayId = 6; // not 5, and not the hardcoded bypass (10)

    SubmitOne(req);

    auto response = DrainOneResponse();
    EXPECT_EQ(response.Message->Status, 'R');
    (void)responsesPool.release(response.Message);
}

// Documents a deliberate (per the "TODO: Remove the hardcoded gateway"
// comment in ProcessMessage) backdoor: gateway id 10 always bypasses the
// authorized-gateway/already-connected check, regardless of what
// Authorized/Connected actually hold. Worth knowing this exists and is
// exercised, not just assumed away as dead code.
TEST_F(ClientRequestProcessorTest, AllowsGatewayIdTenRegardlessOfAuthorization)
{
    auto rawKey = MakeKey(5);
    SeedClient(4, /*authorizedGateway=*/5, /*connected=*/2, Sha256(rawKey));

    naoto::RoutedAuthRequest req{};
    req.RequestType = 'A';
    req.ClientId = 4;
    req.Key = rawKey;
    req.ClientFd = 9;
    req.GatewayId = 10; // hardcoded bypass

    SubmitOne(req);

    auto response = DrainOneResponse();
    EXPECT_EQ(response.Message->Status, 'A')
        << "gateway 10 bypasses the authorized/connected check entirely";
    (void)responsesPool.release(response.Message);
}

TEST_F(ClientRequestProcessorTest, DisconnectPersistsToClientStates)
{
    SeedClient(5, /*authorizedGateway=*/1, /*connected=*/7, MakeKey(6));

    naoto::RoutedAuthRequest req{};
    req.RequestType = 'D';
    req.ClientId = 5;
    req.ClientFd = 0;
    req.GatewayId = 7;

    SubmitOne(req);

    EXPECT_EQ(states.GetClientState(5).GetConnected(), -1)
        << "a disconnect should persist - Connected should reset to -1 "
           "so a later reconnect attempt isn't permanently refused";
}

TEST_F(ClientRequestProcessorTest, DisconnectDoesNotAffectFunds)
{
    // The fix routes the disconnect through SetClientState(), which
    // computes Confirmed/Attempt deltas relative to the current state -
    // make sure that doesn't have any side effect on funds when only
    // Connected actually changed.
    SeedClient(5, /*authorizedGateway=*/1, /*connected=*/7, MakeKey(6), {1, 2, 3},
               {100, 200, 300});

    naoto::RoutedAuthRequest req{};
    req.RequestType = 'D';
    req.ClientId = 5;
    req.ClientFd = 0;
    req.GatewayId = 7;

    SubmitOne(req);

    auto state = states.GetClientState(5);
    EXPECT_EQ(state.Confirmed[0], 100);
    EXPECT_EQ(state.Confirmed[1], 200);
    EXPECT_EQ(state.Confirmed[2], 300);
}

TEST_F(ClientRequestProcessorTest, UnrecognizedRequestTypeIsIgnoredWithoutCrashing)
{
    naoto::RoutedAuthRequest req{};
    req.RequestType = 'X';
    req.ClientId = 6;
    req.ClientFd = 0;
    req.GatewayId = 1;

    EXPECT_NO_FATAL_FAILURE(SubmitOne(req));

    naoto::account_service::RoutedMessage<naoto::ClientAccountSnapshot<naoto::MaxPositions>> msg;
    EXPECT_FALSE(responsesSend.try_dequeue(msg))
        << "an unrecognized request type shouldn't produce a response";
}

TEST_F(ClientRequestProcessorTest, BatchOfMultipleRequestsAllProcessed)
{
    auto key1 = MakeKey(10);
    auto key2 = MakeKey(20);
    SeedClient(7, 1, -1, Sha256(key1));
    SeedClient(8, 1, -1, Sha256(key2));

    MessageBatch *batch = messagesPool.acquire();
    ASSERT_NE(batch, nullptr);
    batch->setSize(2);

    naoto::RoutedAuthRequest req1{};
    req1.RequestType = 'A';
    req1.ClientId = 7;
    req1.Key = key1;
    req1.ClientFd = 1;
    req1.GatewayId = 1;
    (*batch)[0] = req1;

    naoto::RoutedAuthRequest req2{};
    req2.RequestType = 'A';
    req2.ClientId = 8;
    req2.Key = key2;
    req2.ClientFd = 2;
    req2.GatewayId = 1;
    (*batch)[1] = req2;

    ASSERT_TRUE(incoming.try_enqueue(batch));
    processor.ProcessMessage();

    auto r1 = DrainOneResponse();
    auto r2 = DrainOneResponse();

    EXPECT_EQ(r1.Message->Status, 'A');
    EXPECT_EQ(r2.Message->Status, 'A');
    // order isn't guaranteed to matter here, but both client ids should
    // show up across the two responses
    EXPECT_NE(r1.Message->ClientId, r2.Message->ClientId);

    (void)responsesPool.release(r1.Message);
    (void)responsesPool.release(r2.Message);
}
} // namespace naoto::account_service
