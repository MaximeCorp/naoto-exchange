#include <gtest/gtest.h>
#include <client_account_snapshot.hpp>
#include <market_server/order_gateway/include/client_states.hpp>
#include <market_server/order_gateway/include/client_states_writer.hpp>
#include <object_batch.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_auth_request.hpp>
#include <storage_pool.hpp>

using naoto::ClientAccountSnapshot;
using naoto::ObjectBatch;
using naoto::OrderState;
using naoto::OrderStateReport;
using naoto::RoutedAuthRequest;
using naoto::StoragePool;
using naoto::order_gateway::ClientStates;
using naoto::order_gateway::ClientStatesWriter;

namespace
{
    constexpr size_t kMaxPositions = 3;
    constexpr size_t kBatchSize = 4;
    constexpr size_t kBufferSize = 8; // must be a power of 2

    OrderStateReport MakeReport(uint32_t clientId, uint64_t sequenceId,
                                int64_t boughtDelta, uint16_t boughtAssetId,
                                int64_t soldDelta, int64_t soldAttemptDelta,
                                uint16_t soldAssetId, OrderState state)
    {
        OrderStateReport r{};
        r.FillReport(boughtDelta, soldDelta, soldAttemptDelta, sequenceId,
                     clientId, /*orderId=*/1, /*tradeId=*/1, boughtAssetId,
                     soldAssetId, state);
        return r;
    }

    ClientAccountSnapshot<kMaxPositions>
    MakeSnapshot(uint32_t clientId, uint32_t clientFd, uint64_t sequenceId,
                 std::array<uint16_t, kMaxPositions> assetIds,
                 std::array<int64_t, kMaxPositions> confirmed)
    {
        ClientAccountSnapshot<kMaxPositions> s{};
        s.Status = 'A';
        s.SequenceId = sequenceId;
        s.ClientId = clientId;
        s.ClientFd = clientFd;
        s.AssetId = assetIds;
        s.Confirmed = confirmed;
        s.Attempt = {};
        return s;
    }

    class ClientStatesWriterOrderGatewayTest : public ::testing::Test
    {
    protected:
        using ResponseBatch =
            ObjectBatch<ClientAccountSnapshot<kMaxPositions>, kBatchSize>;
        using Writer =
            ClientStatesWriter<64, kMaxPositions, kBatchSize, kBufferSize>;

        ClientStates<kMaxPositions> states{64};
        moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>
            incomingReports{64};
        StoragePool<OrderStateReport> reportPool{64};
        moodycamel::BlockingReaderWriterCircularBuffer<ResponseBatch *>
            incomingResponses{16};
        StoragePool<ResponseBatch> responsePool{16};
        moodycamel::BlockingReaderWriterCircularBuffer<uint32_t>
            incomingDisconnects{16};
        moodycamel::BlockingReaderWriterCircularBuffer<RoutedAuthRequest *>
            outgoingReq{16};
        StoragePool<RoutedAuthRequest> gatewayReqPool{16};

        Writer writer{states,
                      incomingReports,
                      reportPool,
                      incomingResponses,
                      responsePool,
                      incomingDisconnects,
                      outgoingReq,
                      gatewayReqPool};

        // Registers a client (fills ClientsFd + seeds States) the only
        // way the public API allows: through a connection-accepted
        // response, matching how a real client connection would arrive.
        void ConnectClient(uint32_t clientId, uint32_t clientFd,
                            std::array<uint16_t, kMaxPositions> assetIds,
                            std::array<int64_t, kMaxPositions> confirmed)
        {
            ResponseBatch *batch = responsePool.acquire();
            ASSERT_NE(batch, nullptr);
            batch->setSize(1);
            batch->setFd(clientFd);
            (*batch)[0] =
                MakeSnapshot(clientId, clientFd, /*sequenceId=*/0, assetIds,
                             confirmed);

            writer.Handle(batch);
            ASSERT_TRUE(responsePool.release(batch));
        }
    };
} // namespace

TEST_F(ClientStatesWriterOrderGatewayTest, ConnectResponsePopulatesStatesAndClientsFdMapping)
{
    ConnectClient(/*clientId=*/100, /*clientFd=*/5, {1, 2, 3}, {100, 200, 300});

    auto state = states.GetClientState(5);
    EXPECT_EQ(state.ClientId, 100u);
    EXPECT_EQ(state.Confirmed[0], 100);
    EXPECT_EQ(state.Auth, 1u);
}

TEST_F(ClientStatesWriterOrderGatewayTest, MarketReportRoutesThroughClientsFdMapping)
{
    ConnectClient(100, 5, {1, 2, 3}, {100, 200, 300});

    OrderStateReport report =
        MakeReport(/*clientId=*/100, /*sequenceId=*/1, /*boughtDelta=*/10,
                   /*boughtAssetId=*/1, /*soldDelta=*/0, /*soldAttemptDelta=*/0,
                   /*soldAssetId=*/1, OrderState::FILL);

    writer.Handle(&report);

    auto state = states.GetClientState(5);
    EXPECT_EQ(state.Confirmed[0], 110)
        << "report keyed by ClientId should route to the fd it maps to";
}

TEST_F(ClientStatesWriterOrderGatewayTest, ReportForUnknownClientIsDiscardedSafely)
{
    // No ConnectClient() call - ClientsFd has nothing in it.
    OrderStateReport report =
        MakeReport(/*clientId=*/999, 1, 10, 1, 0, 0, 1, OrderState::FILL);

    EXPECT_NO_FATAL_FAILURE(writer.Handle(&report));

    // Nothing should have changed for fd 0 (or any fd) as a side effect.
    auto state = states.GetClientState(0);
    EXPECT_EQ(state.ClientId, 0u);
}

TEST_F(ClientStatesWriterOrderGatewayTest, AddStateIgnoresSoldAttemptDelta)
{
    ConnectClient(100, 5, {1, 2, 3}, {100, 200, 300});

    OrderStateReport report =
        MakeReport(100, 1, 0, 1, -30, -30, 2, OrderState::ADD);

    writer.Handle(&report);

    auto state = states.GetClientState(5);
    EXPECT_EQ(state.Attempt[1], 0)
        << "OrderState::ADD should ignore SoldAttemptDelta (already "
           "accounted for by the local attempt counter) - unlike "
           "account_service's writer, which does not special-case this";
    EXPECT_EQ(state.Confirmed[1], 170); // SoldDelta still applies to Confirmed
}

TEST_F(ClientStatesWriterOrderGatewayTest, BatchHandleAppliesAllThenFlushesEachClientOnce)
{
    ConnectClient(100, 5, {1, 2, 3}, {0, 0, 0});
    ConnectClient(200, 6, {1, 2, 3}, {0, 0, 0});

    OrderStateReport r0 = MakeReport(100, 1, 10, 1, 0, 0, 1, OrderState::FILL);
    OrderStateReport r1 = MakeReport(200, 2, 20, 1, 0, 0, 1, OrderState::FILL);
    OrderStateReport r0b = MakeReport(100, 3, 5, 1, 0, 0, 1, OrderState::FILL);

    std::array<OrderStateReport *, kBatchSize> batch{&r0, &r1, &r0b, nullptr};
    writer.Handle(batch, 3);

    EXPECT_EQ(states.GetClientState(5).Confirmed[0], 15);
    EXPECT_EQ(states.GetClientState(6).Confirmed[0], 20);
}

TEST_F(ClientStatesWriterOrderGatewayTest, DisconnectClearsAuth)
{
    ConnectClient(100, 5, {1, 2, 3}, {0, 0, 0});
    ASSERT_EQ(states.GetClientAuth(5), 1u);

    ASSERT_TRUE(incomingDisconnects.try_enqueue(5));
    writer.TryConsumeDisconnects();

    EXPECT_EQ(states.GetClientAuth(5), 0u);
}

TEST_F(ClientStatesWriterOrderGatewayTest, StaleResponseDoesNotAbortRestOfBatch)
{
    ConnectClient(100, 5, {1, 2, 3}, {0, 0, 0});
    // Bump LastSeq away from 0 so a SequenceId of 0 below is no longer
    // treated as "no activity yet" and instead looks genuinely stale.
    OrderStateReport bump = MakeReport(100, 50, 0, 1, 0, 0, 1, OrderState::FILL);
    writer.Handle(&bump);

    ResponseBatch *batch = responsePool.acquire();
    ASSERT_NE(batch, nullptr);
    batch->setSize(2);

    // First entry: stale (SequenceId 0 doesn't match UpdatesBuffer and
    // isn't LastSeq(50)) - should trigger the resend path but not stop
    // the rest of the batch from being processed.
    (*batch)[0] = MakeSnapshot(100, 5, /*sequenceId=*/0, {1, 2, 3}, {999, 0, 0});
    // Second entry: an otherwise-perfectly-valid new client connection,
    // unrelated to the first. SequenceId must equal LastSeq(50) here -
    // that's the "no order activity since this response was generated"
    // case that makes it non-stale; using 0 like the first entry (an
    // earlier mistake in this test) would make *both* entries
    // legitimately stale by the code's own logic, since
    // UpdatesBuffer[0] never gets set to 0 by anything in this
    // scenario - not a useful test of "does the second entry still get
    // processed".
    (*batch)[1] =
        MakeSnapshot(200, 6, /*sequenceId=*/50, {1, 2, 3}, {500, 0, 0});

    writer.Handle(batch);
    (void)responsePool.release(batch);

    EXPECT_EQ(states.GetClientState(6).ClientId, 200u)
        << "client 200's connection response should still be processed "
           "even though it came after a stale entry in the same batch";
    EXPECT_EQ(states.GetClientState(6).Confirmed[0], 500);

    RoutedAuthRequest *req = nullptr;
    ASSERT_TRUE(outgoingReq.try_dequeue(req))
        << "the stale entry should still have triggered a resend request";
    EXPECT_EQ(req->ClientId, 100u);
    (void)gatewayReqPool.release(req);
}

TEST_F(ClientStatesWriterOrderGatewayTest, TryConsumeReportDrivesHandleFromTheQueue)
{
    ConnectClient(100, 5, {1, 2, 3}, {0, 0, 0});

    OrderStateReport *report = reportPool.acquire();
    ASSERT_NE(report, nullptr);
    *report = MakeReport(100, 1, 42, 1, 0, 0, 1, OrderState::FILL);
    ASSERT_TRUE(incomingReports.try_enqueue(report));

    // ClientStatesWriter privately aliases its two Consumer<> bases as
    // ReportBase/ResponseBase, so a plain writer.TryConsume() is
    // ambiguous from outside the class (both bases declare it). Naming
    // the exact base (which is publicly inherited, so this is legal
    // even though the alias itself is private) disambiguates.
    static_cast<naoto::Consumer<Writer, OrderStateReport, kBatchSize> &>(
        writer)
        .TryConsume();

    EXPECT_EQ(states.GetClientState(5).Confirmed[0], 42);
}
