#include <gtest/gtest.h>
#include <order_risk_check.hpp>

using naoto::Order;
using naoto::OrderAction;
using naoto::OrderSide;
using naoto::OrderType;
using naoto::order_gateway::CheckOrderRisk;
using naoto::order_gateway::ClientState;
using naoto::order_gateway::LocalAttempts;
using naoto::order_gateway::OrderConfirmationStatus;

namespace
{
    constexpr size_t kMaxPositions = 4;
    constexpr size_t kMaxAsset = 100;

    ClientState<kMaxPositions> MakeState(
        uint32_t clientId, uint32_t sessionId,
        std::array<uint16_t, kMaxPositions> assetIds,
        std::array<int64_t, kMaxPositions> confirmed,
        std::array<int64_t, kMaxPositions> attempt = {})
    {
        ClientState<kMaxPositions> s;
        s.ClientId = clientId;
        s.SessionId = sessionId;
        s.AssetId = assetIds;
        s.Confirmed = confirmed;
        s.Attempt = attempt;
        s.Auth = 1;
        return s;
    }

    Order MakeOrder(uint32_t clientId, OrderSide side, OrderType type,
                     int64_t price, uint32_t amount, uint16_t assetId)
    {
        Order o{};
        o.ClientId = clientId;
        o.Side = side;
        o.Type = type;
        o.Price = price;
        o.Amount = amount;
        o.AssetId = assetId;
        o.Action = OrderAction::EXECUTE;
        return o;
    }
} // namespace

TEST(OrderRiskCheckTest, RejectsWhenNotAuthenticated)
{
    ClientState<kMaxPositions> state = MakeState(1, 0, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order = MakeOrder(1, OrderSide::BUY, OrderType::LIMIT, 10, 5, 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, /*auth=*/0);

    EXPECT_EQ(status, OrderConfirmationStatus::UserNotConnected);
}

TEST(OrderRiskCheckTest, RejectsOnClientIdMismatch)
{
    ClientState<kMaxPositions> state = MakeState(1, 0, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order = MakeOrder(/*clientId=*/2, OrderSide::BUY, OrderType::LIMIT,
                             10, 5, 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::BadClientId);
}

TEST(OrderRiskCheckTest, RejectsUnknownAssetBelowMaxAssetAsMaxPositions)
{
    ClientState<kMaxPositions> state = MakeState(1, 0, {1, 2, 3}, {1000, 1000, 1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    // assetId 50 < kMaxAsset(100) but isn't one of this client's
    // positions - the client simply doesn't have that asset tracked.
    Order order = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 5, 50);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::MaxPositions);
}

TEST(OrderRiskCheckTest, RejectsUnknownAssetAtOrAboveMaxAssetAsUnknownSymbol)
{
    ClientState<kMaxPositions> state = MakeState(1, 0, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order =
        MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 5, kMaxAsset + 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::UnknownSymbol);
}

TEST(OrderRiskCheckTest, AcceptsSellWithinConfirmedFunds)
{
    ClientState<kMaxPositions> state = MakeState(1, 0, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, /*price=*/
                             999999, /*amount=*/500, 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::Accepted);
    EXPECT_EQ(localAttempt[0], 500)
        << "sell risk amount is order.Amount, price should not factor in";
}

TEST(OrderRiskCheckTest, RejectsSellExceedingConfirmedFunds)
{
    ClientState<kMaxPositions> state = MakeState(1, 0, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 1001, 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::InsufficientFunds);
    EXPECT_EQ(localAttempt[0], 0) << "a rejected order must not reserve funds";
}

TEST(OrderRiskCheckTest, BuyLimitRiskIsAmountTimesPrice)
{
    // BUY orders always look up asset index 0 (the "currency you're
    // paying with" - see MarketBuyOrdersAreRiskCheckedAgainstAssetZero
    // below, and CheckOrderRisk's unconditional
    // `order.Side == OrderSide::BUY ? 0 : order.AssetId`), regardless of
    // OrderType. The ClientState's tracked position has to be asset id 0
    // for a BUY LIMIT order to be checked against it - using an
    // unrelated id here previously made this test silently check against
    // whatever *other*, unintentionally zero-valued slot happened to
    // match id 0 in the ClientState array, rather than the id(1000)
    // position actually being set up.
    ClientState<kMaxPositions> state = MakeState(1, 0, {0}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    // amount(10) * price(50) = 500, within 1000 confirmed.
    Order order = MakeOrder(1, OrderSide::BUY, OrderType::LIMIT, 50, 10, 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::Accepted);
    EXPECT_EQ(localAttempt[0], 500);
}

TEST(OrderRiskCheckTest, BuyLimitExceedingFundsIsRejected)
{
    // Same asset-id-0 requirement as BuyLimitRiskIsAmountTimesPrice
    // above.
    ClientState<kMaxPositions> state = MakeState(1, 0, {0}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    // amount(10) * price(101) = 1010 > 1000.
    Order order = MakeOrder(1, OrderSide::BUY, OrderType::LIMIT, 101, 10, 1);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::InsufficientFunds);
}

TEST(OrderRiskCheckTest, MarketBuyOrdersAreRiskCheckedAgainstAssetZero)
{
    // A market buy's assetId gets forced to 0 regardless of
    // order.AssetId - this models "the currency you're paying with",
    // consistent with the risk check needing an upper-bound price for
    // market orders elsewhere in the matching engine.
    ClientState<kMaxPositions> state =
        MakeState(1, 0, {0, 7}, {1000, 5000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order = MakeOrder(1, OrderSide::BUY, OrderType::MARKET,
                             /*price=*/100, /*amount=*/5, /*assetId=*/7);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::Accepted);
    EXPECT_EQ(localAttempt[0], 500) << "reserved against asset index 0 (id 0)";
    EXPECT_EQ(localAttempt[1], 0) << "not against the order's own AssetId(7)";
}

TEST(OrderRiskCheckTest, MarketSellOrdersUseTheirOwnAssetId)
{
    // Only MARKET+BUY gets the assetId=0 override; MARKET sells use
    // order.AssetId like limit orders do.
    ClientState<kMaxPositions> state = MakeState(1, 0, {0, 7}, {1000, 5000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;
    Order order = MakeOrder(1, OrderSide::SELL, OrderType::MARKET, 100, 5, 7);

    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::Accepted);
    EXPECT_EQ(localAttempt[1], 5) << "reserved against asset index 1 (id 7)";
}

TEST(OrderRiskCheckTest, LocalAttemptAccumulatesAcrossMultipleOrdersBeforeConfirmation)
{
    // This is the whole point of the local attempt counter: successive
    // orders for the same client, on the same connection, reserve funds
    // immediately without waiting for a round trip to the matching
    // engine and back - so a second order should see the first order's
    // reservation even though ClientState.Attempt itself hasn't moved.
    ClientState<kMaxPositions> state = MakeState(1, 0, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;

    Order first = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 600, 1);
    auto status1 = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, first, 1);
    ASSERT_EQ(status1, OrderConfirmationStatus::Accepted);

    Order second = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 500, 1);
    auto status2 = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, second, 1);

    EXPECT_EQ(status2, OrderConfirmationStatus::InsufficientFunds)
        << "600 + 500 > 1000 confirmed, even though ClientState.Attempt "
           "hasn't been updated yet - the local counter should catch this";
}

TEST(OrderRiskCheckTest, RemoteAttemptIsAddedToLocalAttempt)
{
    // Once a fill/ack round-trips back, ClientState.Attempt reflects it
    // (via FlushTripleBuffer elsewhere) - the risk check must count
    // *both* the remote Attempt and whatever's still outstanding
    // locally, or it'll double-spend.
    ClientState<kMaxPositions> state =
        MakeState(1, 0, {1}, {1000}, /*attempt=*/{400});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;

    Order order = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 601, 1);
    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, order, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::InsufficientFunds)
        << "400 (remote attempt) + 601 (this order) > 1000 confirmed";
}

TEST(OrderRiskCheckTest, BuyLimitOrderWithNegativePriceUnlocksExtraBuyingPower)
{
    // Nothing in CheckOrderRisk (or upstream of it - see order_router.hpp,
    // there's no Price/Amount validation before this function is called)
    // rejects a negative Price. For a BUY LIMIT order the reserved
    // "amount" is order.Amount * order.Price, so a negative price makes
    // that product negative - which always satisfies
    // `amount > confirmed - attempt` as false (Accepted), and then gets
    // *added* to localAttempt, driving it negative instead of reserving
    // funds.
    ClientState<kMaxPositions> state = MakeState(1, 0, {0}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;

    Order negPrice = MakeOrder(1, OrderSide::BUY, OrderType::LIMIT,
                                /*price=*/-1'000'000, /*amount=*/1,
                                /*assetId=*/0);
    auto status1 = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, negPrice, 1);

    EXPECT_EQ(status1, OrderConfirmationStatus::Accepted);
    EXPECT_EQ(localAttempt[0], -1'000'000);

    // With localAttempt[0] now deeply negative, a second order asking
    // for far more than the client's real 1000 confirmed funds still
    // clears the check: confirmed(1000) - attempt(-1,000,000) looks like
    // ~1,001,000 of "available" funds that were never actually there.
    Order overBudget = MakeOrder(1, OrderSide::BUY, OrderType::LIMIT,
                                  /*price=*/1, /*amount=*/10'000,
                                  /*assetId=*/0);
    auto status2 = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, overBudget, 1);

    EXPECT_EQ(status2, OrderConfirmationStatus::Accepted)
        << "a single negative-price order lets a client reserve far more "
           "than their real confirmed funds on every order after it";
}

TEST(OrderRiskCheckTest,
     BuyLimitOrderAmountTimesPriceCanOverflowAndBypassInsufficientFunds)
{
    // order.Amount is uint32_t (up to ~4.29e9) and order.Price is
    // int64_t; CheckOrderRisk computes order.Amount * order.Price with
    // no bounds check on either input (OrderConfirmationStatus even has
    // unused InvalidPrice/InvalidQuantity members that look like they
    // were meant for exactly this, but nothing in the codebase ever
    // returns them). A large-but-individually-plausible-looking Amount
    // and Price multiply out to something past INT64_MAX, wraps to a
    // large negative number, and the InsufficientFunds check
    // (`amount > confirmed - attempt`) can't catch a negative "amount"
    // no matter how little the client actually has confirmed.
    ClientState<kMaxPositions> state = MakeState(1, 0, {0}, {100});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0;

    Order overflowing =
        MakeOrder(1, OrderSide::BUY, OrderType::LIMIT,
                  /*price=*/3'000'000'000LL, /*amount=*/4'000'000'000U,
                  /*assetId=*/0);
    auto status = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, overflowing, 1);

    EXPECT_EQ(status, OrderConfirmationStatus::Accepted)
        << "amount(4e9) * price(3e9) overflows int64_t and wraps negative, "
           "so a client with only 100 confirmed clears the funds check for "
           "an order that should cost far more than that";
}

TEST(OrderRiskCheckTest, SessionIdChangeClearsLocalAttempt)
{
    // A new SessionId (bumped by ClientStates::SetClientState() on
    // reconnect) means the connection got re-established, so whatever
    // was locally reserved on the old connection is stale and must be
    // dropped - otherwise a client could reconnect and have their
    // buying power incorrectly reduced by orders from a previous,
    // already-resolved session.
    ClientState<kMaxPositions> state =
        MakeState(1, /*sessionId=*/1, {1}, {1000});
    LocalAttempts<kMaxPositions> localAttempt;
    uint32_t localSession = 0; // stale, doesn't match state.SessionId yet

    Order first = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 900, 1);
    auto status1 = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, first, 1);
    ASSERT_EQ(status1, OrderConfirmationStatus::Accepted);
    EXPECT_EQ(localSession, 1u) << "localSession should sync to the state's";

    // Simulate a reconnect: bump SessionId again without the local
    // counter having been reset by anything else.
    state.SessionId = 2;

    Order second = MakeOrder(1, OrderSide::SELL, OrderType::LIMIT, 10, 900, 1);
    auto status2 = CheckOrderRisk<kMaxPositions, kMaxAsset>(
        state, localAttempt, localSession, second, 1);

    EXPECT_EQ(status2, OrderConfirmationStatus::Accepted)
        << "the stale 900 reservation from the old session should have "
           "been cleared on the session id change, not summed with it";
    EXPECT_EQ(localAttempt[0], 900);
}
