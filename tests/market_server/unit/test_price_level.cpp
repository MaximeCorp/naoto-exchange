#include <gtest/gtest.h>

#include <csignal>

#include <order.hpp>
#include <order_node.hpp>
#include <price_level.hpp>
#include <single_threaded_storage_pool.hpp>

using naoto::Order;
using naoto::OrderAction;
using naoto::OrderSide;
using naoto::OrderType;
using naoto::matching_engine::OrderNode;
using naoto::matching_engine::PriceLevel;
using naoto::SingleThreadedStoragePool;

namespace
{
    Order MakeOrder(uint64_t orderId, uint32_t amount, int64_t price)
    {
        Order o{};
        o.Price = price;
        o.OrderId = orderId;
        o.ClientOrderId = static_cast<uint32_t>(orderId);
        o.ClientId = 1;
        o.Amount = amount;
        o.AssetId = 1;
        o.Type = OrderType::LIMIT;
        o.Side = OrderSide::BUY;
        o.Action = OrderAction::EXECUTE;
        return o;
    }

    class PriceLevelTest : public ::testing::Test
    {
    protected:
        SingleThreadedStoragePool<OrderNode> pool{16};

        OrderNode *MakeNode(uint64_t orderId, uint32_t amount)
        {
            OrderNode *n = pool.acquire();
            n->SetOrder(MakeOrder(orderId, amount, 100));
            return n;
        }
    };
} // namespace

TEST_F(PriceLevelTest, StartsWithNoOrders)
{
    PriceLevel level(100);
    EXPECT_EQ(level.PeekOrder(), nullptr);
    EXPECT_EQ(level.GetTotalAmount(), 0u);
}

TEST_F(PriceLevelTest, AddOrderUpdatesPeekAndTotalAmount)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);

    level.AddOrder(n1);

    EXPECT_EQ(level.PeekOrder(), n1);
    EXPECT_EQ(level.GetTotalAmount(), 10u);
}

TEST_F(PriceLevelTest, MultipleOrdersMaintainFifoOrder)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    OrderNode *n2 = MakeNode(2, 20);
    OrderNode *n3 = MakeNode(3, 30);

    level.AddOrder(n1);
    level.AddOrder(n2);
    level.AddOrder(n3);

    EXPECT_EQ(level.GetTotalAmount(), 60u);
    EXPECT_EQ(level.PeekOrder(), n1) << "price-time priority: first in, first out";

    OrderNode *popped1 = level.PopOrder();
    EXPECT_EQ(popped1, n1);
    EXPECT_EQ(level.PeekOrder(), n2);

    OrderNode *popped2 = level.PopOrder();
    EXPECT_EQ(popped2, n2);
    EXPECT_EQ(level.PeekOrder(), n3);
}

TEST_F(PriceLevelTest, PopOrderOnEmptyLevelReturnsNull)
{
    PriceLevel level(100);
    EXPECT_EQ(level.PopOrder(), nullptr);
}

TEST_F(PriceLevelTest, PopOrderDecrementsTotalAmount)
{
    // CORRECTION: an earlier version of this test asserted the
    // opposite - that PopOrder() does *not* decrement TotalAmount,
    // based on a misreading of price_level.hpp. It does
    // (`TotalAmount -= res->GetAmount();` is right there in PopOrder()'s
    // body, symmetric with DeleteOrder()). That was a mistake in the
    // test, not a real finding - caught by actually running it rather
    // than trusting the source read. Kept as a real (correct) test of
    // the behavior instead of deleting it outright.
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    level.AddOrder(n1);

    ASSERT_EQ(level.GetTotalAmount(), 10u);
    (void)level.PopOrder();
    EXPECT_EQ(level.GetTotalAmount(), 0u);
}

TEST_F(PriceLevelTest, DeleteOrderFromMiddleRelinksNeighbours)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    OrderNode *n2 = MakeNode(2, 20);
    OrderNode *n3 = MakeNode(3, 30);

    level.AddOrder(n1);
    level.AddOrder(n2);
    level.AddOrder(n3);

    bool becameEmpty = level.DeleteOrder(n2);
    EXPECT_FALSE(becameEmpty);
    EXPECT_EQ(level.GetTotalAmount(), 40u);

    EXPECT_EQ(level.PeekOrder(), n1);
    OrderNode *popped = level.PopOrder();
    EXPECT_EQ(popped, n1);
    EXPECT_EQ(level.PeekOrder(), n3) << "n2 should have been unlinked";
}

TEST_F(PriceLevelTest, DeleteOrderOfLastOrderReportsLevelEmpty)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    level.AddOrder(n1);

    bool becameEmpty = level.DeleteOrder(n1);
    EXPECT_TRUE(becameEmpty);
    EXPECT_EQ(level.PeekOrder(), nullptr);
}

TEST_F(PriceLevelTest, DeleteOrderAtHeadUpdatesHead)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    OrderNode *n2 = MakeNode(2, 20);
    level.AddOrder(n1);
    level.AddOrder(n2);

    (void)level.DeleteOrder(n1);
    EXPECT_EQ(level.PeekOrder(), n2);
}

TEST_F(PriceLevelTest, DeleteOrderAtTailUpdatesTail)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    OrderNode *n2 = MakeNode(2, 20);
    level.AddOrder(n1);
    level.AddOrder(n2);

    (void)level.DeleteOrder(n2);
    // n1 should now be both head and tail; adding a third order should
    // link after n1.
    OrderNode *n3 = MakeNode(3, 30);
    level.AddOrder(n3);

    EXPECT_EQ(level.PeekOrder(), n1);
    (void)level.PopOrder();
    EXPECT_EQ(level.PeekOrder(), n3);
}

TEST_F(PriceLevelTest, GetKeyReturnsConstructedPrice)
{
    PriceLevel level(12345);
    EXPECT_EQ(level.GetKey(), 12345);
}

TEST_F(PriceLevelTest, DefaultConstructedLevelHasZeroPrice)
{
    PriceLevel level;
    EXPECT_EQ(level.GetKey(), 0);
    EXPECT_EQ(level.PeekOrder(), nullptr);
}

TEST_F(PriceLevelTest, SetPriceChangesKey)
{
    PriceLevel level(100);
    level.SetPrice(200);
    EXPECT_EQ(level.GetKey(), 200);
}

TEST_F(PriceLevelTest, IncTotalAmountAppliesPositiveDelta)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    level.AddOrder(n1);

    // Used on the hot path when a resting order is partially filled:
    // the order's own remaining amount is updated separately, and the
    // level's aggregate TotalAmount is adjusted in lockstep rather than
    // walking the list to resum it.
    level.IncTotalAmount(5);
    EXPECT_EQ(level.GetTotalAmount(), 15u);
}

TEST_F(PriceLevelTest, IncTotalAmountAppliesNegativeDelta)
{
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    level.AddOrder(n1);

    // Negative delta: e.g. shrinking TotalAmount to reflect a partial
    // fill on the resting order without popping/re-adding it.
    level.IncTotalAmount(-4);
    EXPECT_EQ(level.GetTotalAmount(), 6u);
}

TEST_F(PriceLevelTest, ClearPriceLevelOnAlreadyEmptyLevelIsANoOp)
{
    SingleThreadedStoragePool<OrderNode> smallPool(2);
    PriceLevel level(100);

    EXPECT_NO_FATAL_FAILURE(level.ClearPriceLevel(smallPool));
    EXPECT_EQ(level.PeekOrder(), nullptr);
    EXPECT_EQ(level.GetTotalAmount(), 0u);

    // Pool must be untouched - clearing an empty level shouldn't
    // consume/release anything.
    EXPECT_NE(smallPool.acquire(), nullptr);
    EXPECT_NE(smallPool.acquire(), nullptr);
}

TEST_F(PriceLevelTest, DeleteOrderOfLastOrderLeavesLevelReusable)
{
    // After the only order is deleted, Head/Tail must both be reset to
    // nullptr (not just Head) - otherwise a subsequent AddOrder that
    // takes the "list not empty" branch would corrupt the list off a
    // stale Tail pointer. Exercise that by re-adding and checking FIFO
    // order still holds with a fresh Tail.
    PriceLevel level(100);
    OrderNode *n1 = MakeNode(1, 10);
    level.AddOrder(n1);
    ASSERT_TRUE(level.DeleteOrder(n1));

    OrderNode *n2 = MakeNode(2, 20);
    OrderNode *n3 = MakeNode(3, 30);
    level.AddOrder(n2);
    level.AddOrder(n3);

    EXPECT_EQ(level.GetTotalAmount(), 50u);
    EXPECT_EQ(level.PeekOrder(), n2);
    EXPECT_EQ(level.PopOrder(), n2);
    EXPECT_EQ(level.PeekOrder(), n3);
}

TEST(PriceLevelDeathTest, ClearPriceLevelTerminatesIfPoolCannotAcceptReleases)
{
    // ClearPriceLevel() releases every node it holds back into the pool
    // it's given. release() on SingleThreadedStoragePool only succeeds
    // while FreeSize < Capacity - a freshly-constructed pool already
    // has FreeSize == Capacity (everything is free), so calling
    // ClearPriceLevel() with the *wrong* pool (anything other than the
    // exact pool the nodes were acquired from) fails on the very first
    // node and terminates the process. This documents that footgun:
    // there is no ownership check, only a capacity check.
    using naoto::matching_engine::PriceLevel;
    SingleThreadedStoragePool<OrderNode> pool(2);
    OrderNode *n1 = pool.acquire();
    n1->SetOrder(MakeOrder(1, 10, 100));

    EXPECT_EXIT(
        {
            PriceLevel level(100);
            level.AddOrder(n1);
            SingleThreadedStoragePool<OrderNode> freshPool(1);
            level.ClearPriceLevel(freshPool);
        },
        ::testing::KilledBySignal(SIGABRT),
        "Failed to release while clearing price level");
}

TEST_F(PriceLevelTest, ClearPriceLevelReturnsAllNodesToThePool)
{
    SingleThreadedStoragePool<OrderNode> smallPool(3);
    PriceLevel level(100);

    OrderNode *n1 = smallPool.acquire();
    OrderNode *n2 = smallPool.acquire();
    OrderNode *n3 = smallPool.acquire();
    n1->SetOrder(MakeOrder(1, 10, 100));
    n2->SetOrder(MakeOrder(2, 10, 100));
    n3->SetOrder(MakeOrder(3, 10, 100));

    level.AddOrder(n1);
    level.AddOrder(n2);
    level.AddOrder(n3);

    ASSERT_EQ(smallPool.acquire(), nullptr) << "pool should be exhausted";

    level.ClearPriceLevel(smallPool);

    // Every node the level held should come back to the pool - clearing
    // a level shouldn't leak nodes. (Previously the loop released every
    // node except the last one it walked to; fixed in price_level.hpp.)
    OrderNode *r1 = smallPool.acquire();
    OrderNode *r2 = smallPool.acquire();
    OrderNode *r3 = smallPool.acquire();
    EXPECT_NE(r1, nullptr);
    EXPECT_NE(r2, nullptr);
    EXPECT_NE(r3, nullptr);
}
