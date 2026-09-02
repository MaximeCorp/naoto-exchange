#include <gtest/gtest.h>
#include <epoll_server.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// See epoll_server.hpp's FRIEND_TEST block: the fixture below and every
// TEST_F(EpollServerTest, ...) case have to live directly in namespace
// naoto (not nested in an anonymous namespace) for the friendship to
// actually apply - same lesson as bid_ask.hpp's FRIEND_TEST hooks.
namespace naoto
{
    namespace
    {
        struct TestMsg
        {
            uint64_t value;
        };
        constexpr size_t kBatchSize = 8;
        using Batch = ObjectBatch<TestMsg, kBatchSize>;

        // CRTP-derived server recording every hook invocation, so tests
        // can assert on what the server actually did without needing to
        // inspect its private state directly.
        class RecordingServer : public EpollServer<RecordingServer, TestMsg,
                                                     kBatchSize>
        {
        public:
            using Base = EpollServer<RecordingServer, TestMsg, kBatchSize>;
            using Base::Base;

            std::vector<uint32_t> AcceptedFds;
            std::vector<uint32_t> ClosedFds;
            std::vector<std::pair<uint32_t, size_t>> BatchHandleCalls;

            void AcceptHandle(uint32_t fd) noexcept
            {
                AcceptedFds.push_back(fd);
            }

            void CloseHandle(uint32_t fd) noexcept
            {
                ClosedFds.push_back(fd);
            }

            void BatchHandle(Batch *batch, uint32_t fd) noexcept
            {
                BatchHandleCalls.emplace_back(fd, batch->getSize());
            }
        };

        // A second msg type used as the handshake/"first message" for
        // the fixture below - equal size to TestMsg so it satisfies
        // EpollServer's `sizeof(T) >= sizeof(InitMessage)` static_assert
        // with T=TestMsg.
        struct InitMsg
        {
            uint64_t token;
        };

        // CRTP-derived server exercising the InitMessage/
        // FirstMessageHandle path, which - unlike everything else in
        // EpollServer - has no coverage anywhere else in this test
        // suite.
        class HandshakeServer
            : public EpollServer<HandshakeServer, TestMsg, kBatchSize,
                                  InitMsg>
        {
        public:
            using Base =
                EpollServer<HandshakeServer, TestMsg, kBatchSize, InitMsg>;
            using Base::Base;

            std::vector<uint32_t> AcceptedFds;
            std::vector<uint32_t> ClosedFds;
            std::vector<std::pair<uint32_t, size_t>> BatchHandleCalls;
            std::vector<std::pair<uint32_t, uint64_t>> FirstMessages;

            void AcceptHandle(uint32_t fd) noexcept
            {
                AcceptedFds.push_back(fd);
            }

            void CloseHandle(uint32_t fd) noexcept
            {
                ClosedFds.push_back(fd);
            }

            void BatchHandle(Batch *batch, uint32_t fd) noexcept
            {
                BatchHandleCalls.emplace_back(fd, batch->getSize());
            }

            void FirstMessageHandle(InitMsg *msg, uint32_t fd) noexcept
            {
                FirstMessages.emplace_back(fd, msg->token);
            }
        };
    } // namespace

    class EpollServerTest : public ::testing::Test
    {
    protected:
        static constexpr int kMaxEvents = 8;
        static constexpr int kMaxPending = 8;
        static constexpr size_t kNbFds = 64;

        StoragePool<Batch> pool{32};
        moodycamel::BlockingReaderWriterCircularBuffer<Batch *> outgoing{32};
        RecordingServer server{/*port=*/0, kMaxEvents, kMaxPending, pool,
                                outgoing, kNbFds};
        std::vector<int> clientSockets;

        StoragePool<Batch> handshakePool{32};
        moodycamel::BlockingReaderWriterCircularBuffer<Batch *>
            handshakeOutgoing{32};
        HandshakeServer handshakeServer{/*port=*/0, kMaxEvents, kMaxPending,
                                        handshakePool, handshakeOutgoing,
                                        kNbFds};
        std::vector<int> handshakeClientSockets;

        void TearDown() override
        {
            for (int fd : clientSockets)
            {
                close(fd);
            }
            for (int fd : handshakeClientSockets)
            {
                close(fd);
            }
        }

        int GetBoundPort()
        {
            struct sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            if (getsockname(server.ListenFd, (struct sockaddr *)&addr, &len)
                != 0)
            {
                return -1;
            }
            return ntohs(addr.sin_port);
        }

        // Connects a real client socket to the server's real (ephemeral,
        // Port=0) listening socket, then drains the pending connection
        // through the server's own accept path directly - bypassing
        // epoll_wait()/startServer()'s infinite loop entirely.
        int ConnectClient()
        {
            int port = GetBoundPort();
            EXPECT_GT(port, 0);

            int clientFd = socket(AF_INET, SOCK_STREAM, 0);
            EXPECT_GE(clientFd, 0);

            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            int rc = connect(clientFd, (struct sockaddr *)&addr, sizeof(addr));
            EXPECT_EQ(rc, 0) << "connect() failed: " << strerror(errno);

            clientSockets.push_back(clientFd);
            server.clientAcceptLoop(server.ListenFd);
            return clientFd;
        }

        uint32_t LastAcceptedServerFd()
        {
            EXPECT_FALSE(server.AcceptedFds.empty());
            return server.AcceptedFds.back();
        }

        void ReadMessage(uint32_t fd)
        {
            server.readMessage(fd);
        }

        std::vector<Batch *> DrainOutgoing()
        {
            std::vector<Batch *> out;
            Batch *b = nullptr;
            while (outgoing.try_dequeue(b))
            {
                out.push_back(b);
            }
            return out;
        }

        // --- Same helpers, for the handshake-enabled server. Kept as
        // fixture members (not free functions) so they inherit
        // EpollServerTest's blanket `friend class EpollServerTest;`
        // grant from epoll_server.hpp without needing their own
        // per-test FRIEND_TEST entries added to the header.
        int GetHandshakeBoundPort()
        {
            struct sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            if (getsockname(handshakeServer.ListenFd,
                             (struct sockaddr *)&addr, &len)
                != 0)
            {
                return -1;
            }
            return ntohs(addr.sin_port);
        }

        int ConnectHandshakeClient()
        {
            int port = GetHandshakeBoundPort();
            EXPECT_GT(port, 0);

            int clientFd = socket(AF_INET, SOCK_STREAM, 0);
            EXPECT_GE(clientFd, 0);

            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            int rc = connect(clientFd, (struct sockaddr *)&addr, sizeof(addr));
            EXPECT_EQ(rc, 0) << "connect() failed: " << strerror(errno);

            handshakeClientSockets.push_back(clientFd);
            handshakeServer.clientAcceptLoop(handshakeServer.ListenFd);
            return clientFd;
        }

        uint32_t LastHandshakeAcceptedServerFd()
        {
            EXPECT_FALSE(handshakeServer.AcceptedFds.empty());
            return handshakeServer.AcceptedFds.back();
        }

        void ReadHandshakeMessage(uint32_t fd)
        {
            handshakeServer.readMessage(fd);
        }

        std::vector<Batch *> DrainHandshakeOutgoing()
        {
            std::vector<Batch *> out;
            Batch *b = nullptr;
            while (handshakeOutgoing.try_dequeue(b))
            {
                out.push_back(b);
            }
            return out;
        }
    };
TEST_F(EpollServerTest, AddClientAddsToEpollAndFiresAcceptHandle)
{
    ConnectClient();

    ASSERT_EQ(server.AcceptedFds.size(), 1u);
    EXPECT_GE(server.AcceptedFds[0], 0u);
}

TEST_F(EpollServerTest, FullMessageProducesOneBatchWithCorrectContent)
{
    int clientFd = ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    TestMsg msg{12345};
    ssize_t sent = send(clientFd, &msg, sizeof(msg), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(sizeof(msg)));

    server.readMessage(srvFd);

    auto batches = DrainOutgoing();
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0]->getFd(), srvFd);
    ASSERT_EQ(batches[0]->getSize(), 1u);
    EXPECT_EQ((*batches[0])[0].value, 12345u);

    ASSERT_TRUE(pool.release(batches[0]));
}

TEST_F(EpollServerTest, MultipleMessagesInOneReadProduceOneBatch)
{
    int clientFd = ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    TestMsg msgs[3] = {{1}, {2}, {3}};
    ssize_t sent = send(clientFd, msgs, sizeof(msgs), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(sizeof(msgs)));

    server.readMessage(srvFd);

    auto batches = DrainOutgoing();
    ASSERT_EQ(batches.size(), 1u)
        << "three messages that arrive in one recv() should coalesce "
           "into a single batch, not three";
    ASSERT_EQ(batches[0]->getSize(), 3u);
    EXPECT_EQ((*batches[0])[0].value, 1u);
    EXPECT_EQ((*batches[0])[1].value, 2u);
    EXPECT_EQ((*batches[0])[2].value, 3u);

    ASSERT_TRUE(pool.release(batches[0]));
}

TEST_F(EpollServerTest, PartialMessageDoesNotEnqueueAnEmptyBatch)
{
    int clientFd = ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    // Send fewer bytes than sizeof(TestMsg) (8 bytes).
    uint8_t partial[4] = {1, 2, 3, 4};
    ssize_t sent = send(clientFd, partial, sizeof(partial), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(sizeof(partial)));

    server.readMessage(srvFd);

    auto batches = DrainOutgoing();
    EXPECT_TRUE(batches.empty())
        << "no complete message has arrived yet, so nothing should be "
           "handed to the consumer";

    // Sending the rest should correctly complete the message and
    // produce exactly one batch.
    uint8_t rest[4] = {5, 6, 7, 8};
    sent = send(clientFd, rest, sizeof(rest), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(sizeof(rest)));

    server.readMessage(srvFd);

    batches = DrainOutgoing();
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0]->getSize(), 1u);
    TestMsg reassembled = (*batches[0])[0];
    uint64_t expected;
    std::memcpy(&expected, partial, 4);
    std::memcpy(reinterpret_cast<uint8_t *>(&expected) + 4, rest, 4);
    EXPECT_EQ(reassembled.value, expected)
        << "the leftover 4 bytes from the first read should combine "
           "with the second read's 4 bytes into one correct message";

    ASSERT_TRUE(pool.release(batches[0]));
}

TEST_F(EpollServerTest, OrderlyCloseFiresRemoveClientAndCloseHandle)
{
    int clientFd = ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    close(clientFd);
    clientSockets.clear(); // already closed, don't double-close in TearDown

    server.readMessage(srvFd);

    ASSERT_EQ(server.ClosedFds.size(), 1u);
    EXPECT_EQ(server.ClosedFds[0], srvFd);
}

TEST_F(EpollServerTest, ReadEventDispatchesToRemoveClientOnHup)
{
    ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    struct epoll_event event{};
    event.events = EPOLLHUP;

    server.readEvent(event, static_cast<int>(srvFd));

    ASSERT_EQ(server.ClosedFds.size(), 1u);
    EXPECT_EQ(server.ClosedFds[0], srvFd);
}

TEST_F(EpollServerTest, BatchHandleHookFiresBeforeEnqueue)
{
    int clientFd = ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    TestMsg msg{7};
    ASSERT_EQ(send(clientFd, &msg, sizeof(msg), 0),
              static_cast<ssize_t>(sizeof(msg)));

    server.readMessage(srvFd);

    ASSERT_EQ(server.BatchHandleCalls.size(), 1u);
    EXPECT_EQ(server.BatchHandleCalls[0].first, srvFd);
    EXPECT_EQ(server.BatchHandleCalls[0].second, 1u);

    for (auto *b : DrainOutgoing())
    {
        (void)pool.release(b);
    }
}

TEST_F(EpollServerTest, MoreMessagesThanOneBatchHoldsProducesTwoBatches)
{
    // kBatchSize is 8: send 10 messages in one write() so they all
    // arrive together, and confirm readMessage() (called once) drains
    // the socket across multiple recv() iterations, producing a full
    // 8-message batch followed by a second 2-message batch - not one
    // truncated batch and two messages silently dropped.
    int clientFd = ConnectClient();
    uint32_t srvFd = LastAcceptedServerFd();

    TestMsg msgs[10];
    for (uint64_t i = 0; i < 10; ++i)
    {
        msgs[i].value = i + 1;
    }
    ASSERT_EQ(send(clientFd, msgs, sizeof(msgs), 0),
              static_cast<ssize_t>(sizeof(msgs)));

    ReadMessage(srvFd);

    auto batches = DrainOutgoing();
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0]->getSize(), 8u);
    EXPECT_EQ(batches[1]->getSize(), 2u);
    for (uint64_t i = 0; i < 8; ++i)
    {
        EXPECT_EQ((*batches[0])[i].value, i + 1);
    }
    for (uint64_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ((*batches[1])[i].value, i + 9);
    }

    for (auto *b : batches)
    {
        (void)pool.release(b);
    }
}

// --- InitMessage / FirstMessageHandle: the handshake path, untested
// anywhere else in this suite --------------------------------------

TEST_F(EpollServerTest, FirstMessageFiresHandshakeHandleAndThenReadsNormally)
{
    int clientFd = ConnectHandshakeClient();
    uint32_t srvFd = LastHandshakeAcceptedServerFd();

    InitMsg init{0xC0FFEE};
    ASSERT_EQ(send(clientFd, &init, sizeof(init), 0),
              static_cast<ssize_t>(sizeof(init)));

    ReadHandshakeMessage(srvFd);

    ASSERT_EQ(handshakeServer.FirstMessages.size(), 1u);
    EXPECT_EQ(handshakeServer.FirstMessages[0].first, srvFd);
    EXPECT_EQ(handshakeServer.FirstMessages[0].second, 0xC0FFEEu);

    // The handshake bytes must not have been mistaken for a normal
    // message batch.
    EXPECT_TRUE(DrainHandshakeOutgoing().empty());

    // A normal message sent afterward should be treated as an ordinary
    // batch, not routed through FirstMessageHandle again.
    TestMsg msg{99};
    ASSERT_EQ(send(clientFd, &msg, sizeof(msg), 0),
              static_cast<ssize_t>(sizeof(msg)));

    ReadHandshakeMessage(srvFd);

    ASSERT_EQ(handshakeServer.FirstMessages.size(), 1u)
        << "FirstMessageHandle must fire exactly once per connection";
    auto batches = DrainHandshakeOutgoing();
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ((*batches[0])[0].value, 99u);

    for (auto *b : batches)
    {
        (void)handshakePool.release(b);
    }
}

TEST_F(EpollServerTest, PartialFirstMessageWaitsForTheRestBeforeHandshaking)
{
    int clientFd = ConnectHandshakeClient();
    uint32_t srvFd = LastHandshakeAcceptedServerFd();

    // sizeof(InitMsg) is 8 bytes; send only 4.
    uint8_t partial[4] = {1, 2, 3, 4};
    ASSERT_EQ(send(clientFd, partial, sizeof(partial), 0),
              static_cast<ssize_t>(sizeof(partial)));

    ReadHandshakeMessage(srvFd);
    EXPECT_TRUE(handshakeServer.FirstMessages.empty())
        << "handshake must not fire on a partial first message";

    uint8_t rest[4] = {5, 6, 7, 8};
    ASSERT_EQ(send(clientFd, rest, sizeof(rest), 0),
              static_cast<ssize_t>(sizeof(rest)));

    ReadHandshakeMessage(srvFd);

    ASSERT_EQ(handshakeServer.FirstMessages.size(), 1u)
        << "the completed handshake message should fire once the rest "
           "of its bytes arrive";
    uint64_t expected;
    std::memcpy(&expected, partial, 4);
    std::memcpy(reinterpret_cast<uint8_t *>(&expected) + 4, rest, 4);
    EXPECT_EQ(handshakeServer.FirstMessages[0].second, expected);
}

} // namespace naoto
