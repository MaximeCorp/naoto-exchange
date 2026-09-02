#include <gtest/gtest.h>
#include <market_server/order_gateway/include/client_delta.hpp>
#include <market_server/order_gateway/include/client_state.hpp>
#include <local_attempts.hpp>

using naoto::order_gateway::ClientDelta;
using naoto::order_gateway::ClientState;
using naoto::order_gateway::LocalAttempts;

constexpr size_t kMaxPositions = 4;

TEST(LocalAttemptsTest, StartsAtZero)
{
    LocalAttempts<kMaxPositions> attempts;
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(attempts[static_cast<uint16_t>(i)], 0);
    }
}

TEST(LocalAttemptsTest, IndexOperatorAllowsReadWrite)
{
    LocalAttempts<kMaxPositions> attempts;
    attempts[2] = 500;
    EXPECT_EQ(attempts[2], 500);
}

TEST(LocalAttemptsTest, ClearResetsAllSlotsToZero)
{
    LocalAttempts<kMaxPositions> attempts;
    attempts[0] = 10;
    attempts[3] = -20;

    attempts.Clear();

    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(attempts[static_cast<uint16_t>(i)], 0);
    }
}

TEST(ClientStateTest, GetAssetIdxFindsMatchingAsset)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};

    size_t idx = 99;
    EXPECT_TRUE(state.GetAssetIdx(30, idx));
    EXPECT_EQ(idx, 2u);
}

TEST(ClientStateTest, GetAssetIdxReturnsFalseForUnknownAsset)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};

    size_t idx = 0;
    EXPECT_FALSE(state.GetAssetIdx(999, idx));
}

TEST(ClientStateTest, GetAssetConfirmedAndAttemptReturnCorrectSlot)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};
    state.Confirmed = {100, 200, 300, 400};
    state.Attempt = {1, 2, 3, 4};

    EXPECT_EQ(state.GetAssetConfirmed(30), 300);
    EXPECT_EQ(state.GetAssetAttempt(30), 3);
}

TEST(ClientStateTest, GetAssetConfirmedReturnsMinusOneForUnknownAsset)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};

    EXPECT_EQ(state.GetAssetConfirmed(999), -1);
    EXPECT_EQ(state.GetAssetAttempt(999), -1);
}

TEST(ClientStateTest, IndexedAccessorsReturnRawSlotValues)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};
    state.Confirmed = {100, 200, 300, 400};
    state.Attempt = {1, 2, 3, 4};

    EXPECT_EQ(state.GetAssetIdAt(1), 20u);
    EXPECT_EQ(state.GetConfirmedAt(1), 200);
    EXPECT_EQ(state.GetAttemptAt(1), 2);
}

TEST(ClientStateTest, DefaultConstructedStateIsUnauthenticated)
{
    ClientState<kMaxPositions> state;
    EXPECT_EQ(state.Auth, 0u);
    EXPECT_EQ(state.SessionId, 0u);
}
