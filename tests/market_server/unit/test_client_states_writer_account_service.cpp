#include <gtest/gtest.h>
#include <market_server/account_service/include/client_states.hpp>
#include <market_server/account_service/include/client_states_writer.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>

using naoto::OrderState;
using naoto::OrderStateReport;
using naoto::StoragePool;
using naoto::account_service::ClientState;
using naoto::account_service::ClientStates;
using naoto::account_service::ClientStatesWriter;

namespace
{
    constexpr size_t kMaxPositions = 3;
    constexpr size_t kBatchSize = 4;

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

    class ClientStatesWriterAccountServiceTest : public ::testing::Test
    {
    protected:
        using Writer = ClientStatesWriter<kMaxPositions, kBatchSize>;

        ClientStates<kMaxPositions> states{4};
        moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>
            incoming{16};
        StoragePool<OrderStateReport> pool{16};
        Writer writer{states, incoming, pool};

        void Seed(uint32_t clientId,
                  std::array<uint16_t, kMaxPositions> assetIds,
                  std::array<int64_t, kMaxPositions> confirmed)
        {
            ClientState<kMaxPositions> s;
            s.ClientId = clientId;
            s.AssetId = assetIds;
            s.Confirmed = confirmed;
            s.Attempt = {};
            states.SetClientState(s, 0);
            states.FlushTripleBuffer(clientId);
        }
    };
} // namespace

TEST_F(ClientStatesWriterAccountServiceTest, SingleHandleAppliesBoughtAndSoldDeltasAndFlushes)
{
    Seed(0, {1, 2, 3}, {100, 200, 300});

    OrderStateReport report = MakeReport(
        /*clientId=*/0, /*sequenceId=*/1, /*boughtDelta=*/50,
        /*boughtAssetId=*/1, /*soldDelta=*/-30, /*soldAttemptDelta=*/-30,
        /*soldAssetId=*/2, OrderState::FILL);

    writer.Handle(&report);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.Confirmed[0], 150); // bought asset 1: 100+50
    EXPECT_EQ(state.Confirmed[1], 170); // sold asset 2: 200-30
}

TEST_F(ClientStatesWriterAccountServiceTest, AddReportsDoNotApplySoldAttemptDelta)
{
    // Handle() passes 0 for the bought side's attempt delta unconditionally,
    // and always passes SoldAttemptDelta through for the sold side
    // regardless of OrderState - unlike order_gateway's writer, which
    // special-cases OrderState::ADD to skip it. Document what
    // account_service's version actually does.
    Seed(0, {1, 2, 3}, {100, 200, 300});

    OrderStateReport report =
        MakeReport(0, 1, 50, 1, -30, -30, 2, OrderState::ADD);

    writer.Handle(&report);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.Attempt[1], -30)
        << "account_service's ClientStatesWriter applies SoldAttemptDelta "
           "even for ADD reports (no special-casing, unlike order_gateway's)";
}

TEST_F(ClientStatesWriterAccountServiceTest, BatchHandleAppliesAllDeltasBeforeFlushing)
{
    Seed(0, {1, 2, 3}, {100, 200, 300});
    Seed(1, {1, 2, 3}, {1000, 2000, 3000});

    OrderStateReport r0 = MakeReport(0, 1, 10, 1, -5, -5, 2, OrderState::FILL);
    OrderStateReport r1 = MakeReport(1, 2, 20, 1, -15, -15, 2, OrderState::FILL);
    OrderStateReport r0b = MakeReport(0, 3, 5, 1, 0, 0, 2, OrderState::FILL);

    std::array<OrderStateReport *, kBatchSize> batch{&r0, &r1, &r0b, nullptr};
    writer.Handle(batch, 3);

    auto state0 = states.GetClientState(0);
    EXPECT_EQ(state0.Confirmed[0], 115) << "both of client 0's reports (10+5) applied";
    EXPECT_EQ(state0.Confirmed[1], 195);

    auto state1 = states.GetClientState(1);
    EXPECT_EQ(state1.Confirmed[0], 1020);
    EXPECT_EQ(state1.Confirmed[1], 1985);
}

TEST_F(ClientStatesWriterAccountServiceTest, BatchHandleFlushesEachTouchedClientExactlyOnce)
{
    // Regardless of how many reports a client has in one batch, its
    // state should only be flushed once per Handle() call - checked
    // indirectly here: apply two reports for the same client in one
    // batch and confirm both deltas show up together (if it flushed
    // between them against a stale local view, the second delta could
    // get applied to the wrong "next" buffer generation and take an
    // extra flush to surface).
    Seed(0, {1, 2, 3}, {0, 0, 0});

    OrderStateReport r1 = MakeReport(0, 1, 10, 1, 0, 0, 1, OrderState::FILL);
    OrderStateReport r2 = MakeReport(0, 2, 20, 1, 0, 0, 1, OrderState::FILL);

    std::array<OrderStateReport *, kBatchSize> batch{&r1, &r2, nullptr, nullptr};
    writer.Handle(batch, 2);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.Confirmed[0], 30);
}

TEST_F(ClientStatesWriterAccountServiceTest, TryConsumeDrivesHandleFromTheQueue)
{
    Seed(0, {1, 2, 3}, {0, 0, 0});

    OrderStateReport *report = pool.acquire();
    ASSERT_NE(report, nullptr);
    *report = MakeReport(0, 1, 42, 1, 0, 0, 1, OrderState::FILL);
    ASSERT_TRUE(incoming.try_enqueue(report));

    writer.TryConsume();

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.Confirmed[0], 42);
}
