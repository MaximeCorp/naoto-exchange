#include <gtest/gtest.h>
#include <client_account_snapshot.hpp>
#include <market_server/order_gateway/include/client_states.hpp>

using naoto::ClientAccountSnapshot;
using naoto::order_gateway::ClientStates;

namespace
{
    constexpr size_t kMaxPositions = 3;

    ClientAccountSnapshot<kMaxPositions> MakeSnapshot(
        uint32_t clientFd, uint32_t clientId,
        std::array<uint16_t, kMaxPositions> assetIds,
        std::array<int64_t, kMaxPositions> confirmed,
        std::array<int64_t, kMaxPositions> attempt)
    {
        ClientAccountSnapshot<kMaxPositions> s{};
        s.Status = 'A';
        s.SequenceId = 0;
        s.ClientId = clientId;
        s.ClientFd = clientFd;
        s.AssetId = assetIds;
        s.Confirmed = confirmed;
        s.Attempt = attempt;
        return s;
    }
} // namespace

// --- FIXED bug: ClientState's default constructor used to leave
// ClientId/AssetId/Confirmed/Attempt uninitialized ----------------------
//
// It used to only initialize Auth and SessionId. ClientId, AssetId,
// Confirmed, and Attempt weren't in the member-init list and have no
// in-class default initializers, so for a class with a user-provided
// constructor they were left with indeterminate values - not zero.
// ClientStates' constructor builds States1/2/3 as
// `std::vector<ClientState<MaxPositions>>(maxClients)`, which
// value-initializes each element by calling exactly this constructor -
// so every "fresh" client (before any SetClientState()) had garbage
// Confirmed/Attempt instead of the zero baseline the whole
// delta-accumulation design assumes. Worse: since SetClientState()
// computes deltas as `response->Confirmed[i] - ref.Confirmed[i]` against
// the *current* (pre-update) state, that garbage baseline poisoned every
// subsequent flush too - under -O3 this test file's other tests
// (SetClientAssetsAppliesIncrementalDeltaAfterFlush,
// MultipleSequentialUpdatesAccumulateCorrectly, etc.) were intermittently
// failing with genuinely wrong Confirmed/Attempt values (not just
// "uninitialized reads happen to look zero-ish"), depending on what was
// sitting on the heap/stack at the time. Fixed in client_state.hpp's
// constructor init list; this test now passes and guards against
// regressing back to it.
TEST(ClientStatesTest, FreshStateStartsAtZero)
{
    ClientStates<kMaxPositions> states(4);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.ClientId, 0u);
    EXPECT_EQ(state.Auth, 0u);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(state.Confirmed[i], 0);
        EXPECT_EQ(state.Attempt[i], 0);
    }
}

// --- CONFIRMED finding: ClientAccountSnapshot's multi-byte fields are
// misaligned by design, and SetClientState() dereferences them directly
// -------------------------------------------------------------------
//
// ClientAccountSnapshot<N> is #pragma pack(push, 1)'d with `char Status`
// first, so every field after it (SequenceId, ClientId/Fd, and the
// Confirmed/Attempt int64_t arrays) sits 1 byte off its natural
// alignment - permanently, regardless of where the struct itself is
// allocated. SetClientState() reads `response->Confirmed[i]` /
// `response->Attempt[i]` directly off a `const ClientAccountSnapshot*`,
// which is undefined behavior for the misaligned int64_t reads (UBSan
// flags this at runtime below; x86 tolerates it silently but slower,
// other architectures may not).
//
// This is exactly the class of issue already called out in the project
// notes for Order ("order binaries are naturally aligned while being
// packed... will do the same for other important binaries soon") -
// ClientAccountSnapshot looks like one of the "other important
// binaries" that TODO hasn't reached yet. The fix mirrors what was done
// for Order: reorder fields so 8-byte members come first (SequenceId,
// Confirmed[], Attempt[]), then 4-byte (ClientId, ClientFd), then
// 2-byte (AssetId[]), then Status last - keeps the struct pack(1)'d
// with zero gaps *and* naturally aligned. Not changed here since it's a
// wire-format change that needs account_service updated in lockstep;
// left as a concretely reproduced, TODO-linked finding instead. This
// test (and every one below that calls SetClientState) will print
// UBSan misaligned-access reports until it's fixed - they're not
// crashes on x86, but they are real UB.
//
// Confirmed empirically NOT the cause of the wrong-Confirmed/Attempt
// values described in the FreshStateStartsAtZero comment above (that
// was the ClientState constructor bug, now fixed): with only that fix
// applied and this alignment issue left exactly as-is, every test below
// passes correctly under -O3 - x86/GCC just quietly tolerates the
// unaligned loads here rather than miscompiling around them. Still
// worth fixing for the reasons above (portability, and matching your
// own stated plan for Order), just not on the critical path the other
// finding was.
TEST(ClientStatesTest, SetClientStateIsInvisibleUntilFlush)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeSnapshot(0, 42, {1, 2, 3}, {100, 200, 300},
                                  {10, 20, 30});

    states.SetClientState(&snapshot);

    // Readers must still see the pre-update (all-zero) state: the
    // producer only writes into the "next" buffer, and nothing is
    // visible until FlushTripleBuffer() flips Complete.
    auto stillOld = states.GetClientState(0);
    EXPECT_EQ(stillOld.ClientId, 0u);
    EXPECT_EQ(stillOld.Confirmed[0], 0);

    states.FlushTripleBuffer(0);

    auto updated = states.GetClientState(0);
    EXPECT_EQ(updated.ClientId, 42u);
    EXPECT_EQ(updated.Auth, 1u);
    EXPECT_EQ(updated.SessionId, 1u);
    EXPECT_EQ(updated.Confirmed[0], 100);
    EXPECT_EQ(updated.Confirmed[1], 200);
    EXPECT_EQ(updated.Confirmed[2], 300);
    EXPECT_EQ(updated.Attempt[0], 10);
    EXPECT_EQ(updated.Attempt[1], 20);
    EXPECT_EQ(updated.Attempt[2], 30);
}

TEST(ClientStatesTest, GetClientFundsFindsAssetAfterFlush)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeSnapshot(0, 42, {1, 2, 3}, {100, 200, 300},
                                  {10, 20, 30});
    states.SetClientState(&snapshot);
    states.FlushTripleBuffer(0);

    int64_t confirmed = -1, attempt = -1;
    ASSERT_TRUE(states.GetClientFunds(0, 2, &confirmed, &attempt));
    EXPECT_EQ(confirmed, 200);
    EXPECT_EQ(attempt, 20);

    EXPECT_FALSE(states.GetClientFunds(0, 999, &confirmed, &attempt));
}

TEST(ClientStatesTest, SetClientAssetsAppliesIncrementalDeltaAfterFlush)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeSnapshot(0, 42, {1, 2, 3}, {100, 200, 300},
                                  {10, 20, 30});
    states.SetClientState(&snapshot);
    states.FlushTripleBuffer(0);

    // Simulate the risk-check hot path bumping the "attempt" amount for
    // asset 2 by +5 and confirming +50 once a trade settles.
    states.SetClientAssets(0, /*confirmed=*/50, /*attempt=*/5, /*assetId=*/2);
    states.FlushTripleBuffer(0);

    int64_t confirmed = -1, attempt = -1;
    ASSERT_TRUE(states.GetClientFunds(0, 2, &confirmed, &attempt));
    EXPECT_EQ(confirmed, 250);
    EXPECT_EQ(attempt, 25);

    // Other assets on the same client must be untouched.
    ASSERT_TRUE(states.GetClientFunds(0, 1, &confirmed, &attempt));
    EXPECT_EQ(confirmed, 100);
    EXPECT_EQ(attempt, 10);
}

TEST(ClientStatesTest, RepeatedFlushesWithoutNewWritesConvergeStably)
{
    // Because each buffer accumulates the sum of all three delta slots
    // on every flush, calling FlushTripleBuffer() repeatedly without any
    // intervening SetClientAssets()/SetClientState() must be a no-op on
    // the observed values (deltas are zeroed once consumed).
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeSnapshot(0, 42, {1, 2, 3}, {100, 200, 300},
                                  {10, 20, 30});
    states.SetClientState(&snapshot);
    states.FlushTripleBuffer(0);

    auto afterFirstFlush = states.GetClientState(0);

    states.FlushTripleBuffer(0);
    states.FlushTripleBuffer(0);
    states.FlushTripleBuffer(0);

    auto afterMoreFlushes = states.GetClientState(0);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(afterMoreFlushes.Confirmed[i], afterFirstFlush.Confirmed[i])
            << "asset idx " << i;
        EXPECT_EQ(afterMoreFlushes.Attempt[i], afterFirstFlush.Attempt[i])
            << "asset idx " << i;
    }
}

TEST(ClientStatesTest, MultipleSequentialUpdatesAccumulateCorrectly)
{
    // Drive the triple buffer through several SetClientAssets + Flush
    // rounds (mirroring the risk-check hot path: local attempt bumps
    // followed by periodic flushes) and check the final numbers are
    // exactly the sum of every delta applied, regardless of the
    // three-buffer rotation underneath.
    ClientStates<kMaxPositions> states(2);
    auto snapshot = MakeSnapshot(0, 7, {5, 6, 7}, {1000, 0, 0}, {0, 0, 0});
    states.SetClientState(&snapshot);
    states.FlushTripleBuffer(0);

    int64_t expectedConfirmed = 1000;
    int64_t expectedAttempt = 0;

    for (int round = 0; round < 10; ++round)
    {
        int64_t confirmedDelta = round * 3;
        int64_t attemptDelta = round;
        states.SetClientAssets(0, confirmedDelta, attemptDelta, 5);
        expectedConfirmed += confirmedDelta;
        expectedAttempt += attemptDelta;
        states.FlushTripleBuffer(0);
    }

    int64_t confirmed = -1, attempt = -1;
    ASSERT_TRUE(states.GetClientFunds(0, 5, &confirmed, &attempt));
    EXPECT_EQ(confirmed, expectedConfirmed);
    EXPECT_EQ(attempt, expectedAttempt);
}

TEST(ClientStatesTest, SetAuthStatusIsVisibleAfterFlush)
{
    ClientStates<kMaxPositions> states(4);
    auto snapshot = MakeSnapshot(0, 42, {1, 2, 3}, {0, 0, 0}, {0, 0, 0});
    states.SetClientState(&snapshot);
    states.FlushTripleBuffer(0);

    ASSERT_EQ(states.GetClientAuth(0), 1u);

    states.SetAuthStatus(0, 0);
    states.FlushTripleBuffer(0);

    EXPECT_EQ(states.GetClientAuth(0), 0u);
}

TEST(ClientStatesTest, DifferentClientsAreIndependent)
{
    ClientStates<kMaxPositions> states(4);
    auto snap0 = MakeSnapshot(0, 1, {1, 2, 3}, {100, 0, 0}, {0, 0, 0});
    auto snap1 = MakeSnapshot(1, 2, {1, 2, 3}, {500, 0, 0}, {0, 0, 0});

    states.SetClientState(&snap0);
    states.SetClientState(&snap1);
    states.FlushTripleBuffer(0);
    states.FlushTripleBuffer(1);

    int64_t confirmed = -1, attempt = -1;
    ASSERT_TRUE(states.GetClientFunds(0, 1, &confirmed, &attempt));
    EXPECT_EQ(confirmed, 100);

    ASSERT_TRUE(states.GetClientFunds(1, 1, &confirmed, &attempt));
    EXPECT_EQ(confirmed, 500);
}

TEST(ClientStatesTest, ConstructingFromExistingSnapshotSeedsAllThreeBuffers)
{
    std::vector<naoto::order_gateway::ClientState<kMaxPositions>> initial(2);
    initial[0].ClientId = 99;
    initial[0].AssetId = {1, 2, 3};
    initial[0].Confirmed = {10, 20, 30};
    initial[0].Attempt = {1, 2, 3};
    initial[0].Auth = 1;

    ClientStates<kMaxPositions> states(2, initial);

    auto state = states.GetClientState(0);
    EXPECT_EQ(state.ClientId, 99u);
    EXPECT_EQ(state.Confirmed[1], 20);
    EXPECT_EQ(state.Auth, 1u);
}
