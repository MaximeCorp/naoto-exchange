#include <gtest/gtest.h>

#include <bid_ask.hpp>
#include <object_batch.hpp>
#include <order.hpp>
#include <order_book_update.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>

#include <vector>

using naoto::Order;
using naoto::OrderAction;
using naoto::OrderBookUpdate;
using naoto::OrderSide;
using naoto::OrderState;
using naoto::OrderStateReport;
using naoto::OrderType;
using naoto::StoragePool;
using naoto::matching_engine::BidAsk;
using naoto::matching_engine::OrderNode;

namespace
{
    constexpr size_t kFHMSize = 8;
    constexpr size_t kSkipListMaxLevel = 4;
    constexpr size_t kBatchSize = 8;
    constexpr size_t kOrderMapSize = 16;

    // Convention used throughout these tests (and, per bid_ask.hpp's
    // price-gated matching loop, required in production too - see
    // MarketBuyOrderWithZeroPriceNeverMatches_LIKELY_BUG below): a
    // "don't care about price" market order still needs a Price that
    // won't lose to any real resting price.
    constexpr int64_t kMarketBuyPrice = 1'000'000'000;
    constexpr int64_t kMarketSellPrice = -1'000'000'000;

    Order MakeOrder(uint64_t orderId, uint32_t clientId, OrderSide side,
                     OrderType type, int64_t price, uint32_t amount)
    {
        Order o{};
        o.Price = price;
        o.Timestamp = 0;
        o.OrderId = orderId;
        o.ClientOrderId = static_cast<uint32_t>(orderId);
        o.ClientId = clientId;
        o.Amount = amount;
        o.AssetId = 1;
        o.Type = type;
        o.Side = side;
        o.Action = OrderAction::EXECUTE;
        return o;
    }

    Order MakeCancel(uint64_t orderIdToCancel, uint32_t clientId,
                      OrderSide side)
    {
        Order o{};
        // Per bid_ask.hpp: "order.Amount field is reused for cancel
        // requests to represent the id of the order that we want to
        // cancel".
        o.Amount = static_cast<uint32_t>(orderIdToCancel);
        o.ClientId = clientId;
        o.Side = side;
        o.Action = OrderAction::CANCEL;
        return o;
    }
} // namespace

// The FRIEND_TEST(BidAskTest, ...) declarations inside bid_ask.hpp live in
// namespace naoto::matching_engine, and friend name lookup binds to a
// class in that *exact* namespace (not e.g. an anonymous namespace
// nested inside it). So the fixture and every TEST_F(BidAskTest, ...)
// below have to be written directly in naoto::matching_engine for the
// friendship to actually take effect -- otherwise every private-member
// access below fails to compile with "is private within this context".
namespace naoto::matching_engine
{
    class BidAskTest : public ::testing::Test
    {
    protected:
        using Engine = BidAsk<kFHMSize, kSkipListMaxLevel, kBatchSize,
                               kOrderMapSize>;

        moodycamel::BlockingReaderWriterCircularBuffer<
            naoto::ObjectBatch<Order, kBatchSize> *>
            incoming{4};
        moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>
            outgoingOrders{256};
        moodycamel::BlockingReaderWriterCircularBuffer<OrderBookUpdate *>
            outgoingBook{256};

        StoragePool<naoto::ObjectBatch<Order, kBatchSize>> ordersPool{4};
        StoragePool<OrderStateReport> reportsPool{256};
        StoragePool<OrderBookUpdate> bookUpdatesPool{256};

        std::unique_ptr<Engine> engine;

        void SetUp() override
        {
            engine = std::make_unique<Engine>(
                /*assetId=*/1, /*initialPrice=*/100, incoming, outgoingOrders,
                outgoingBook, ordersPool, reportsPool, bookUpdatesPool,
                /*orderNodePoolSize=*/64, /*skipListNodesPoolSize=*/64);
        }

        std::vector<OrderStateReport> DrainReports()
        {
            std::vector<OrderStateReport> out;
            OrderStateReport *r = nullptr;
            while (outgoingOrders.try_dequeue(r))
            {
                out.push_back(*r);
                bool released = reportsPool.release(r);
                (void)released;
            }
            return out;
        }

        std::vector<OrderBookUpdate> DrainBookUpdates()
        {
            std::vector<OrderBookUpdate> out;
            OrderBookUpdate *u = nullptr;
            while (outgoingBook.try_dequeue(u))
            {
                out.push_back(*u);
                bool released = bookUpdatesPool.release(u);
                (void)released;
            }
            return out;
        }

        static bool HasState(const std::vector<OrderStateReport> &reports,
                              uint32_t orderId, OrderState state)
        {
            for (auto &r : reports)
            {
                if (r.OrderId == orderId && r.State == state)
                {
                    return true;
                }
            }
            return false;
        }
    };

TEST_F(BidAskTest, NonMarketableBuyRestsInBook)
{
    Order buy = MakeOrder(1, /*clientId=*/10, OrderSide::BUY,
                           OrderType::LIMIT, /*price=*/100, /*amount=*/10);

    engine->executeOrder(buy);

    OrderNode *best = engine->Bid.GetBestOffer();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->GetId(), 1u);
    EXPECT_EQ(best->GetAmount(), 10u);
    EXPECT_EQ(engine->Ask.GetBestOffer(), nullptr);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::ADD);
    EXPECT_EQ(reports[0].ClientId, 10u);
    EXPECT_EQ(reports[0].SoldAttemptDelta, 1000); // amount(10) * price(100)
}

TEST_F(BidAskTest, MarketableBuyCrossesRestingAsk)
{
    Order sell = MakeOrder(1, /*clientId=*/20, OrderSide::SELL,
                            OrderType::LIMIT, /*price=*/100, /*amount=*/10);
    engine->executeOrder(sell);
    DrainReports();
    DrainBookUpdates();

    Order buy = MakeOrder(2, /*clientId=*/21, OrderSide::BUY,
                           OrderType::LIMIT, /*price=*/100, /*amount=*/6);
    engine->executeOrder(buy);

    // The resting ask should be partially filled (10 - 6 = 4 left), the
    // marketable buy should be fully filled and never rest.
    OrderNode *restingAsk = engine->Ask.GetBestOffer();
    ASSERT_NE(restingAsk, nullptr);
    EXPECT_EQ(restingAsk->GetId(), 1u);
    EXPECT_EQ(restingAsk->GetAmount(), 4u);
    EXPECT_EQ(engine->Bid.GetBestOffer(), nullptr)
        << "the marketable buy order must not rest in the book";

    auto reports = DrainReports();
    EXPECT_TRUE(HasState(reports, 2, OrderState::FILL))
        << "the fully-filled aggressor order should be reported FILL";
    EXPECT_TRUE(HasState(reports, 1, OrderState::PARTIAL_FILL))
        << "the partially consumed resting order should be PARTIAL_FILL";
}

TEST_F(BidAskTest, NonMarketableSellRestsInBook)
{
    Order sell = MakeOrder(1, /*clientId=*/10, OrderSide::SELL,
                            OrderType::LIMIT, /*price=*/100, /*amount=*/10);

    engine->executeOrder(sell);

    OrderNode *best = engine->Ask.GetBestOffer();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->GetId(), 1u);
    EXPECT_EQ(engine->Bid.GetBestOffer(), nullptr);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::ADD);
}

TEST_F(BidAskTest, MarketableSellCrossesRestingBid)
{
    Order buy = MakeOrder(1, /*clientId=*/20, OrderSide::BUY,
                           OrderType::LIMIT, /*price=*/100, /*amount=*/10);
    engine->executeOrder(buy);
    DrainReports();

    Order sell = MakeOrder(2, /*clientId=*/21, OrderSide::SELL,
                            OrderType::LIMIT, /*price=*/100, /*amount=*/4);
    engine->executeOrder(sell);

    OrderNode *restingBid = engine->Bid.GetBestOffer();
    ASSERT_NE(restingBid, nullptr);
    EXPECT_EQ(restingBid->GetAmount(), 6u);
    EXPECT_EQ(engine->Ask.GetBestOffer(), nullptr);

    auto reports = DrainReports();
    EXPECT_TRUE(HasState(reports, 2, OrderState::FILL));
    EXPECT_TRUE(HasState(reports, 1, OrderState::PARTIAL_FILL));
}

TEST_F(BidAskTest, PriceTimePriorityFifoAtSameLevel)
{
    Order sell1 = MakeOrder(1, /*clientId=*/1, OrderSide::SELL,
                             OrderType::LIMIT, 100, 5);
    Order sell2 = MakeOrder(2, /*clientId=*/2, OrderSide::SELL,
                             OrderType::LIMIT, 100, 5);
    engine->executeOrder(sell1);
    engine->executeOrder(sell2);
    DrainReports();

    // A marketable buy for exactly the first order's size should match
    // only the first (earlier) resting order, never the second.
    Order buy = MakeOrder(3, /*clientId=*/3, OrderSide::BUY,
                           OrderType::LIMIT, 100, 5);
    engine->executeOrder(buy);

    OrderNode *remaining = engine->Ask.GetBestOffer();
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->GetId(), 2u)
        << "order 1 (first in, same price) should have been consumed "
           "first, leaving order 2 resting";
    EXPECT_EQ(remaining->GetAmount(), 5u);
}

TEST_F(BidAskTest, MarketOrderSweepsMultiplePriceLevels)
{
    Order cheapSell = MakeOrder(1, 1, OrderSide::SELL, OrderType::LIMIT, 100,
                                 5);
    Order pricierSell = MakeOrder(2, 2, OrderSide::SELL, OrderType::LIMIT,
                                   105, 5);
    engine->executeOrder(cheapSell);
    engine->executeOrder(pricierSell);
    DrainReports();

    Order marketBuy = MakeOrder(3, 3, OrderSide::BUY, OrderType::MARKET,
                                 kMarketBuyPrice, 8);
    engine->executeOrder(marketBuy);

    // Should have consumed all 5 of the cheaper level, then 3 of the 5
    // at the pricier level, in price priority order - leaving 2 units
    // of order 2 resting as the new best (only) offer.
    OrderNode *remaining = engine->Ask.GetBestOffer();
    ASSERT_NE(remaining, nullptr) << "cheap level fully gone, pricier "
                                      "level partially consumed and left";
    EXPECT_EQ(remaining->GetId(), 2u);
    EXPECT_EQ(remaining->GetAmount(), 2u);

    auto reports = DrainReports();
    EXPECT_TRUE(HasState(reports, 3, OrderState::FILL));
    EXPECT_TRUE(HasState(reports, 1, OrderState::FILL))
        << "order 1 (cheap level) should be fully consumed";
    EXPECT_TRUE(HasState(reports, 2, OrderState::PARTIAL_FILL))
        << "order 2 (pricier level) should be partially consumed (3 of 5)";
}

TEST_F(BidAskTest, MarketOrderWithNoLiquidityIsFullyCancelled)
{
    Order marketBuy = MakeOrder(1, 1, OrderSide::BUY, OrderType::MARKET,
                                 kMarketBuyPrice, 10);
    engine->executeOrder(marketBuy);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::CANCEL);
    EXPECT_EQ(reports[0].ClientId, 1u);
    EXPECT_EQ(reports[0].OrderId, 1u);
}

// --- Suspected bug: a marketable limit order's unmatched remainder is
// cancelled instead of resting in the book -----------------------------
//
// The project notes describe the intended behaviour explicitly:
// "limit marketable orders are treated as market orders until market
// price gets worse than the order's price then as a limit order". But
// FillBuyOrder()/FillSellOrder() are only ever reached via
// ExecuteMarketableOrder(), and whatever amount is left once the order
// stops being marketable gets a CANCEL report - there's no call back
// into AddLimitOrder() to rest the remainder. This test pins down what
// actually happens today; if the described "becomes a limit order"
// behavior is what you want, FillBuyOrder/FillSellOrder's tail
// (`if (order.Amount > 0) { ...CANCEL... }`) is where to add it.
TEST_F(BidAskTest,
       MarketableLimitBuyPartialFillCancelsRemainder_LIKELY_BUG)
{
    Order sell = MakeOrder(1, 1, OrderSide::SELL, OrderType::LIMIT, 100, 5);
    engine->executeOrder(sell);
    DrainReports();

    // Marketable (price >= best ask) limit buy for more than what's
    // resting.
    Order buy = MakeOrder(2, 2, OrderSide::BUY, OrderType::LIMIT, 100, 10);
    engine->executeOrder(buy);

    EXPECT_EQ(engine->Bid.GetBestOffer(), nullptr)
        << "today, the unfilled remainder is cancelled rather than resting "
           "as a limit order - see comment above if that's not intended";
    EXPECT_EQ(engine->Ask.GetBestOffer(), nullptr)
        << "the resting sell should have been fully consumed (5 of the "
           "order's 10)";

    auto reports = DrainReports();
    EXPECT_TRUE(HasState(reports, 2, OrderState::CANCEL))
        << "remaining 5 units of the buy order get cancelled instead of "
           "resting";
    EXPECT_TRUE(HasState(reports, 1, OrderState::FILL));
}

TEST_F(BidAskTest, CancelOrderRemovesRestingBuyFromBook)
{
    Order buy = MakeOrder(1, 42, OrderSide::BUY, OrderType::LIMIT, 100, 10);
    engine->executeOrder(buy);
    DrainReports();

    Order cancel = MakeCancel(/*orderIdToCancel=*/1, /*clientId=*/42,
                               OrderSide::BUY);
    engine->CancelOrder<OrderSide::BUY>(cancel);

    EXPECT_EQ(engine->Bid.GetBestOffer(), nullptr);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::CANCEL);
    EXPECT_EQ(reports[0].OrderId, 1u);
}

TEST_F(BidAskTest, CancelOrderRemovesRestingSellFromBook)
{
    Order sell = MakeOrder(1, 42, OrderSide::SELL, OrderType::LIMIT, 100, 10);
    engine->executeOrder(sell);
    DrainReports();

    Order cancel = MakeCancel(1, 42, OrderSide::SELL);
    engine->CancelOrder<OrderSide::SELL>(cancel);

    EXPECT_EQ(engine->Ask.GetBestOffer(), nullptr);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::CANCEL);
}

// --- CONFIRMED bug (crashed the process under ASan before the fix in
// bid_ask.hpp) -----------------------------------------------------------
//
// CancelOrder() had a second, unguarded
// `toCancel->GetClientId() != order.ClientId` check inside the rejection
// branch, used only to decide which log message to print. Unlike the
// first check (which is safely `!found || ...`, short-circuited), this
// one ran unconditionally - so a cancel for an unknown order id
// (found == false, toCancel == nullptr) null-dereferenced and took down
// the whole matching engine process. This is directly reachable from
// client input (cancel an order that's already filled, already
// cancelled, or just made up) - not a contrived edge case. Fixed
// in-place in bid_ask.hpp; this test is what caught it (it SEGV'd under
// ASan before the fix).
TEST_F(BidAskTest, CancelOrderRejectedForUnknownOrderId)
{
    Order cancel = MakeCancel(/*orderIdToCancel=*/999, /*clientId=*/1,
                               OrderSide::BUY);
    engine->CancelOrder<OrderSide::BUY>(cancel);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::REJECT);
}

TEST_F(BidAskTest, CancelOrderRejectedForWrongClientId)
{
    Order buy = MakeOrder(1, /*clientId=*/42, OrderSide::BUY,
                           OrderType::LIMIT, 100, 10);
    engine->executeOrder(buy);
    DrainReports();

    Order cancel = MakeCancel(/*orderIdToCancel=*/1, /*clientId=*/999,
                               OrderSide::BUY);
    engine->CancelOrder<OrderSide::BUY>(cancel);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::REJECT);

    // The original order must still be resting - a rejected cancel must
    // not have any side effect on the book.
    OrderNode *stillResting = engine->Bid.GetBestOffer();
    ASSERT_NE(stillResting, nullptr);
    EXPECT_EQ(stillResting->GetId(), 1u);
}

TEST_F(BidAskTest, ExactFillRemovesRestingOrderFromBook)
{
    Order sell = MakeOrder(1, 1, OrderSide::SELL, OrderType::LIMIT, 100, 5);
    engine->executeOrder(sell);
    DrainReports();

    Order buy = MakeOrder(2, 2, OrderSide::BUY, OrderType::LIMIT, 100, 5);
    engine->executeOrder(buy);

    EXPECT_EQ(engine->Ask.GetBestOffer(), nullptr);
    EXPECT_EQ(engine->Bid.GetBestOffer(), nullptr);

    auto reports = DrainReports();
    EXPECT_TRUE(HasState(reports, 1, OrderState::FILL));
    EXPECT_TRUE(HasState(reports, 2, OrderState::FILL));
}

// --- Suspected bug: market orders with an unset/zero Price never match
// -------------------------------------------------------------------
//
// IsMarketable() correctly treats any OrderType::MARKET order as
// marketable regardless of price. But FillBuyOrder()'s outer loop has
// its own, unconditional gate:
//
//   if (BestAskPrice > order.Price) [[unlikely]] { break; }
//
// which does not check order.Type at all. A market buy order with the
// "natural" default Price of 0 will hit this immediately against any
// positively-priced resting ask and never match - it gets silently
// cancelled instead of filled, despite there being plenty of liquidity.
// The workaround (used elsewhere in this file) is for market buy/sell
// orders to carry a sentinel Price (e.g. a very large/very negative
// number) - but nothing enforces that convention here or at the
// gateway, so a client (or a future refactor) that leaves Price at its
// default silently loses the order. Worth either enforcing the sentinel
// centrally or making the outer-loop break conditional on
// order.Type == OrderType::LIMIT.
TEST_F(BidAskTest, MarketBuyOrderWithZeroPriceNeverMatches_LIKELY_BUG)
{
    Order sell = MakeOrder(1, 1, OrderSide::SELL, OrderType::LIMIT, 100, 5);
    engine->executeOrder(sell);
    DrainReports();

    Order marketBuy = MakeOrder(2, 2, OrderSide::BUY, OrderType::MARKET,
                                 /*price=*/0, /*amount=*/5);
    engine->executeOrder(marketBuy);

    auto reports = DrainReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].State, OrderState::CANCEL)
        << "a zero-price market order gets fully cancelled instead of "
           "matching the resting liquidity below it - see comment above";

    OrderNode *stillResting = engine->Ask.GetBestOffer();
    ASSERT_NE(stillResting, nullptr)
        << "the resting sell order should be untouched, since the market "
           "order above never actually matched it";
    EXPECT_EQ(stillResting->GetAmount(), 5u);
}
} // namespace naoto::matching_engine
