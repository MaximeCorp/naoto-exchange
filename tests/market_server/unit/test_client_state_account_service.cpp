#include <gtest/gtest.h>
#include <market_server/account_service/include/client_state.hpp>

#include <openssl/sha.h>

using naoto::account_service::ClientState;

constexpr size_t kMaxPositions = 4;

TEST(ClientStateAccountServiceTest, DefaultConstructedIsFullyZeroed)
{
    ClientState<kMaxPositions> state;

    EXPECT_EQ(state.ClientId, 0u);
    EXPECT_EQ(state.GetAuthorized(), 0);
    EXPECT_EQ(state.GetConnected(), 0);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(state.Key[i], 0);
    }
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(state.GetAssetIdAt(i), 0u);
        EXPECT_EQ(state.GetConfirmedAt(i), 0);
        EXPECT_EQ(state.GetAttemptAt(i), 0);
    }
}

TEST(ClientStateAccountServiceTest, ParameterizedConstructorsFillEveryPosition)
{
    // The 2-arg and 3-arg constructors should store confirmed/attempt
    // uniformly across every position, matching what SetConfirmed()/
    // SetAttempt() already establish as this class's meaning for
    // "set confirmed/attempt from a single scalar".
    ClientState<kMaxPositions> a(42, 1000);
    EXPECT_EQ(a.ClientId, 42u);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(a.GetConfirmedAt(i), 1000);
    }

    ClientState<kMaxPositions> b(43, 1000, 500);
    EXPECT_EQ(b.ClientId, 43u);
    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(b.GetConfirmedAt(i), 1000);
        EXPECT_EQ(b.GetAttemptAt(i), 500);
    }
}

TEST(ClientStateAccountServiceTest, SetConfirmedFillsEveryPosition)
{
    // Exercises the SetConfirmed()/SetAttempt() bug fix directly - these
    // used to try `Confirmed = confirmed;` (a std::array assigned from a
    // scalar), which doesn't compile. Never called anywhere in the
    // codebase today, so the compile error was latent.
    ClientState<kMaxPositions> state;

    state.SetConfirmed(777);
    state.SetAttempt(88);

    for (size_t i = 0; i < kMaxPositions; ++i)
    {
        EXPECT_EQ(state.GetConfirmedAt(i), 777);
        EXPECT_EQ(state.GetAttemptAt(i), 88);
    }
}

TEST(ClientStateAccountServiceTest, SetClientIdAuthorizedConnectedRoundTrip)
{
    ClientState<kMaxPositions> state;

    state.SetClientId(9);
    state.SetAuthorized(3);
    state.SetConnected(7);

    EXPECT_EQ(state.GetClientId(), 9u);
    EXPECT_EQ(state.GetAuthorized(), 3);
    EXPECT_EQ(state.GetConnected(), 7);
}

TEST(ClientStateAccountServiceTest, GetAssetConfirmedAndAttemptFindMatchingAsset)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};
    state.Confirmed = {100, 200, 300, 400};
    state.Attempt = {1, 2, 3, 4};

    EXPECT_EQ(state.GetAssetConfirmed(30), 300);
    EXPECT_EQ(state.GetAssetAttempt(30), 3);
}

TEST(ClientStateAccountServiceTest, GetAssetConfirmedReturnsMinusOneForUnknownAsset)
{
    ClientState<kMaxPositions> state;
    state.AssetId = {10, 20, 30, 40};

    EXPECT_EQ(state.GetAssetConfirmed(999), -1);
    EXPECT_EQ(state.GetAssetAttempt(999), -1);
}

TEST(ClientStateAccountServiceTest, CheckKeyAcceptsTheRightPreimageAndRejectsOthers)
{
    ClientState<kMaxPositions> state;

    std::array<uint8_t, 32> secret{};
    for (size_t i = 0; i < 32; ++i)
    {
        secret[i] = static_cast<uint8_t>(i * 7 + 1);
    }

    // CheckKey() hashes the argument with SHA-256 and compares against
    // the stored Key - so Key must already hold the *hash* of the
    // secret, not the secret itself.
    unsigned char expectedHash[SHA256_DIGEST_LENGTH];
    SHA256(secret.data(), secret.size(), expectedHash);
    std::copy(expectedHash, expectedHash + 32, state.Key.begin());

    EXPECT_TRUE(state.CheckKey(secret));

    std::array<uint8_t, 32> wrong = secret;
    wrong[0] ^= 0xFF;
    EXPECT_FALSE(state.CheckKey(wrong));
}

TEST(ClientStateAccountServiceTest, CheckKeyOnDefaultStateRejectsArbitraryKey)
{
    // With the constructor fix, Key starts at all-zero (not garbage),
    // so an arbitrary non-empty key should reliably fail to match.
    ClientState<kMaxPositions> state;

    std::array<uint8_t, 32> someKey{};
    someKey[0] = 1;

    EXPECT_FALSE(state.CheckKey(someKey));
}
