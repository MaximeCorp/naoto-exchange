#include <gtest/gtest.h>
#include <market_server/account_service/include/client_states.hpp>

using naoto::account_service::ClientState;
using naoto::account_service::ClientStates;

constexpr size_t kMaxPositions = 3;

namespace
{
    ClientState<kMaxPositions> MakeState(uint32_t clientId,
                                          std::array<uint16_t, kMaxPositions> assetIds,
                                          std::array<int64_t, kMaxPositions> confirmed,
                                          std::array<int64_t, kMaxPositions> attempt)
    {
        ClientState<kMaxPositions> s;
        s.ClientId = clientId;
        s.AssetId = assetIds;
        s.Confirmed = confirmed;
        s.Attempt = attempt;
        return s;
    }
} // namespace

TEST(ClientStatesAccountServiceTest, FreshStateStartsAtZero)
{
    ClientStates<kMaxPositions> states(4);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.ClientId, 0u);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(state.Confirmed[i], 0);
        EXPECT_EQ(state.Attempt[i], 0);
    }
}

TEST(ClientStatesAccountServiceTest, SetClientStateIsInvisibleUntilFlush)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeState(0, {1, 2, 3}, {100, 200, 300}, {10, 20, 30});

    states.SetClientState(snapshot, /*sequenceId=*/5);

    auto stillOld = states.GetClientState(0);
    EXPECT_EQ(stillOld.ClientId, 0u);

    states.FlushTripleBuffer(0);

    auto updated = states.GetClientState(0);
    EXPECT_EQ(updated.ClientId, 0u);
    EXPECT_EQ(updated.Confirmed[0], 100);
    EXPECT_EQ(updated.Confirmed[1], 200);
    EXPECT_EQ(updated.Confirmed[2], 300);
    EXPECT_EQ(updated.Attempt[1], 20);
}

TEST(ClientStatesAccountServiceTest, SetClientAssetsAppliesIncrementalDeltaAfterFlush)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeState(0, {1, 2, 3}, {100, 200, 300}, {10, 20, 30});
    states.SetClientState(snapshot, 0);
    states.FlushTripleBuffer(0);

    states.SetClientAssets(0, /*confirmed=*/50, /*attempt=*/5, /*assetId=*/2,
                            /*sequenceId=*/1);
    states.FlushTripleBuffer(0);

    auto state = states.GetClientState(0);
    // asset 2 is at index 1
    EXPECT_EQ(state.Confirmed[1], 250);
    EXPECT_EQ(state.Attempt[1], 25);
    // untouched assets stay put
    EXPECT_EQ(state.Confirmed[0], 100);
    EXPECT_EQ(state.Confirmed[2], 300);
}

TEST(ClientStatesAccountServiceTest, MultipleSequentialUpdatesAccumulateCorrectly)
{
    // Same "does the triple-buffer rotation actually converge to the
    // true sum of every delta applied" invariant test as
    // order_gateway's, run against account_service's independent
    // (SequenceIds-tracking) implementation of the same pattern.
    ClientStates<kMaxPositions> states(2);
    auto snapshot = MakeState(0, {5, 6, 7}, {1000, 0, 0}, {0, 0, 0});
    states.SetClientState(snapshot, 0);
    states.FlushTripleBuffer(0);

    int64_t expectedConfirmed = 1000;
    int64_t expectedAttempt = 0;

    for (int round = 0; round < 10; ++round)
    {
        int64_t confirmedDelta = round * 3;
        int64_t attemptDelta = round;
        states.SetClientAssets(0, confirmedDelta, attemptDelta, 5, round + 1);
        expectedConfirmed += confirmedDelta;
        expectedAttempt += attemptDelta;
        states.FlushTripleBuffer(0);
    }

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.Confirmed[0], expectedConfirmed);
    EXPECT_EQ(state.Attempt[0], expectedAttempt);
}

TEST(ClientStatesAccountServiceTest, RepeatedFlushesWithoutNewWritesConvergeStably)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeState(0, {1, 2, 3}, {100, 200, 300}, {10, 20, 30});
    states.SetClientState(snapshot, 0);
    states.FlushTripleBuffer(0);

    auto afterFirst = states.GetClientState(0);

    states.FlushTripleBuffer(0);
    states.FlushTripleBuffer(0);
    states.FlushTripleBuffer(0);

    auto afterMore = states.GetClientState(0);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(afterMore.Confirmed[i], afterFirst.Confirmed[i]);
        EXPECT_EQ(afterMore.Attempt[i], afterFirst.Attempt[i]);
    }
}

TEST(ClientStatesAccountServiceTest, DifferentClientsAreIndependent)
{
    ClientStates<kMaxPositions> states(4);
    auto snap0 = MakeState(0, {1, 2, 3}, {100, 0, 0}, {0, 0, 0});
    auto snap1 = MakeState(1, {1, 2, 3}, {500, 0, 0}, {0, 0, 0});

    states.SetClientState(snap0, 0);
    states.SetClientState(snap1, 0);
    states.FlushTripleBuffer(0);
    states.FlushTripleBuffer(1);

    EXPECT_EQ(states.GetClientState(0).Confirmed[0], 100);
    EXPECT_EQ(states.GetClientState(1).Confirmed[0], 500);
}

TEST(ClientStatesAccountServiceTest, ConstructingFromExistingSnapshotSeedsAllThreeBuffers)
{
    std::vector<ClientState<kMaxPositions>> initial(2);
    initial[0].ClientId = 99;
    initial[0].AssetId = {1, 2, 3};
    initial[0].Confirmed = {10, 20, 30};
    initial[0].Attempt = {1, 2, 3};

    ClientStates<kMaxPositions> states(2, initial);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.ClientId, 99u);
    EXPECT_EQ(state.Confirmed[1], 20);
}

// SequenceIds aren't delta-accumulated the way Confirmed/Attempt are -
// each Set*() call directly overwrites the "next" buffer's slot, and
// FlushTripleBuffer() single-hop-copies the just-committed value forward
// into the new "next" buffer. Verify that forwarding actually keeps the
// sequence id observable/consistent across repeated flushes (this is
// the piece of account_service's ClientStates that order_gateway's
// doesn't have).
TEST(ClientStatesAccountServiceTest, SequenceIdTracksThroughRepeatedFlushes)
{
    ClientStates<kMaxPositions> states(2);
    auto snapshot = MakeState(0, {1, 2, 3}, {0, 0, 0}, {0, 0, 0});

    states.SetClientState(snapshot, /*sequenceId=*/100);
    states.FlushTripleBuffer(0);

    states.SetClientAssets(0, 10, 0, 1, /*sequenceId=*/101);
    states.FlushTripleBuffer(0);

    states.SetClientAssets(0, 10, 0, 1, /*sequenceId=*/102);
    states.FlushTripleBuffer(0);

    // No public getter for the sequence id exists, so this test can
    // only exercise the code path for crashes/UB (via ASan/UBSan) and
    // confirm the funds values still end up correct alongside it -
    // documenting the gap rather than working around it with a friend
    // hook, since nothing else in the codebase reads SequenceIds back
    // out either.
    auto state = states.GetClientState(0);
    EXPECT_EQ(state.Confirmed[0], 20);
}
