#include <gtest/gtest.h>
#include <flat_hash_map.hpp>
#include <order.hpp>
#include <order_book.hpp>
#include <order_node.hpp>
#include <single_threaded_storage_pool.hpp>

using naoto::FlatHashMap;
using naoto::Order;
using naoto::OrderAction;
using naoto::OrderSide;
using naoto::OrderType;
using naoto::matching_engine::OrderBook;
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

    constexpr size_t kFHMSize = 8;
    constexpr size_t kSkipListMaxLevel = 4;
    constexpr size_t kOrderMapSize = 16;

    class OrderBookTest : public ::testing::Test
    {
    protected:
        SingleThreadedStoragePool<OrderNode> nodePool{64};
        SingleThreadedStoragePool<PriceLevel> levelPool{64};
        FlatHashMap<uint64_t, OrderNode *, kOrderMapSize> orderMap;
        OrderBook<kFHMSize, kSkipListMaxLevel, kOrderMapSize> book{
            32, nodePool, levelPool, orderMap};

        // Mirrors what BidAsk does around AddLimitOrder(): the OrderMap
        // entry is the caller's responsibility to add (OrderBook only
        // ever *removes* from it, inside DeleteOrder()), so tests that
        // want to exercise the OrderMap side of DeleteOrder() need to
        // populate it the same way production code does.
        OrderNode *AddLimit(uint64_t orderId, uint32_t amount, int64_t price)
        {
            OrderNode *n = nodePool.acquire();
            n->SetOrder(MakeOrder(orderId, amount, price));
            orderMap.AddNode(orderId, n);
            (void)book.AddLimitOrder(n);
            return n;
        }
    };
} // namespace

TEST_F(OrderBookTest, EmptyBookHasNoBestOffer)
{
    EXPECT_EQ(book.GetBestOffer(), nullptr);
    EXPECT_EQ(book.GetBestLevel(), nullptr);
}

TEST_F(OrderBookTest, AddLimitOrderCreatesPriceLevelAndBestOffer)
{
    OrderNode *n = AddLimit(1, 10, 100);

    ASSERT_NE(book.GetBestLevel(), nullptr);
    EXPECT_EQ(book.GetBestLevel()->GetKey(), 100);
    EXPECT_EQ(book.GetBestOffer(), n);
}

TEST_F(OrderBookTest, SecondOrderAtSamePriceSharesTheLevel)
{
    OrderNode *n1 = AddLimit(1, 10, 100);
    OrderNode *n2 = AddLimit(2, 5, 100);

    PriceLevel *level = book.GetBestLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->GetTotalAmount(), 15u);
    EXPECT_EQ(book.GetBestOffer(), n1) << "FIFO within the level";
    (void)n2;
}

TEST_F(OrderBookTest, BestLevelUnderDefaultLessIsLowestPrice)
{
    // Default OrderBook<...> uses std::less<int64_t>, i.e. this models
    // the Ask side: the "best" offer is the lowest price.
    AddLimit(1, 10, 105);
    AddLimit(2, 10, 95);
    AddLimit(3, 10, 100);

    EXPECT_EQ(book.GetBestLevel()->GetKey(), 95);
}

TEST_F(OrderBookTest, DeleteOrderRemovesFromLevelAndKeepsLevelWhenNotEmpty)
{
    OrderNode *n1 = AddLimit(1, 10, 100);
    AddLimit(2, 5, 100);

    PriceLevel *remaining = book.DeleteOrder(n1);
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->GetTotalAmount(), 5u);
    EXPECT_EQ(book.GetBestLevel(), remaining);

    // DeleteOrder() must also drop the OrderMap entry for the order it
    // just removed, regardless of whether the level survives.
    OrderNode *found = nullptr;
    EXPECT_FALSE(orderMap.GetVal(1, found));

    // The order that's still resting must be untouched in OrderMap.
    OrderNode *stillMapped = nullptr;
    EXPECT_TRUE(orderMap.GetVal(2, stillMapped));
}

TEST_F(OrderBookTest, DeleteLastOrderAtLevelRemovesTheLevelEntirely)
{
    OrderNode *n1 = AddLimit(1, 10, 100);

    PriceLevel *result = book.DeleteOrder(n1);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(book.GetBestLevel(), nullptr);
    EXPECT_EQ(book.GetBestOffer(), nullptr);

    OrderNode *found = nullptr;
    EXPECT_FALSE(orderMap.GetVal(1, found))
        << "OrderMap must not retain a stale entry for a deleted order";
}

TEST_F(OrderBookTest, DeleteLastOrderAtLevelReleasesTheOrderNodeBackToItsPool)
{
    // Use a tiny node pool so a leaked node shows up immediately as a
    // pool that can't be fully reacquired, rather than only showing up
    // after a long run.
    SingleThreadedStoragePool<OrderNode> tinyNodePool{2};
    SingleThreadedStoragePool<PriceLevel> tinyLevelPool{2};
    FlatHashMap<uint64_t, OrderNode *, kOrderMapSize> tinyOrderMap;
    OrderBook<kFHMSize, kSkipListMaxLevel, kOrderMapSize> tinyBook{
        4, tinyNodePool, tinyLevelPool, tinyOrderMap};

    OrderNode *n1 = tinyNodePool.acquire();
    n1->SetOrder(MakeOrder(1, 10, 100));
    tinyOrderMap.AddNode(1, n1);
    (void)tinyBook.AddLimitOrder(n1);

    // Sole order at its price level - deleting it must empty (and free)
    // the level, and must also return the OrderNode to OrderNodePool.
    PriceLevel *result = tinyBook.DeleteOrder(n1);
    ASSERT_EQ(result, nullptr);

    OrderNode *first = tinyNodePool.acquire();
    OrderNode *second = tinyNodePool.acquire();
    EXPECT_NE(first, nullptr);
    EXPECT_NE(second, nullptr)
        << "both pool slots should be free again - the deleted order's "
           "node must have been released, not leaked";
}

TEST_F(OrderBookTest, DeleteOrderOnUnknownPriceLevelStillReleasesTheOrderNode)
{
    // A node whose price was never actually AddLimitOrder()'d into this
    // book (FastMap.GetVal() will miss). This shouldn't be reachable in
    // practice, but DeleteOrder() has an explicit "not found" branch for
    // it, and that branch must still give the OrderNode back to the pool
    // rather than leaking it just because the price lookup failed.
    SingleThreadedStoragePool<OrderNode> tinyNodePool{1};
    SingleThreadedStoragePool<PriceLevel> tinyLevelPool{1};
    FlatHashMap<uint64_t, OrderNode *, kOrderMapSize> tinyOrderMap;
    OrderBook<kFHMSize, kSkipListMaxLevel, kOrderMapSize> tinyBook{
        4, tinyNodePool, tinyLevelPool, tinyOrderMap};

    OrderNode *n1 = tinyNodePool.acquire();
    n1->SetOrder(MakeOrder(1, 10, 999)); // never added via AddLimitOrder
    tinyOrderMap.AddNode(1, n1);

    PriceLevel *result = tinyBook.DeleteOrder(n1);
    EXPECT_EQ(result, nullptr);

    EXPECT_NE(tinyNodePool.acquire(), nullptr)
        << "the sole node pool slot should be free again even on the "
           "not-found path";

    OrderNode *found = nullptr;
    EXPECT_FALSE(tinyOrderMap.GetVal(1, found));
}

TEST_F(OrderBookTest, MultiplePriceLevelsBestLevelUpdatesAfterDeletion)
{
    OrderNode *low = AddLimit(1, 10, 95);
    AddLimit(2, 10, 100);
    AddLimit(3, 10, 105);

    ASSERT_EQ(book.GetBestLevel()->GetKey(), 95);

    book.DeleteOrder(low);
    EXPECT_EQ(book.GetBestLevel()->GetKey(), 100);
}

TEST_F(OrderBookTest, DescendingComparatorModelsBidSideBestIsHighestPrice)
{
    SingleThreadedStoragePool<OrderNode> nodePool2{64};
    SingleThreadedStoragePool<PriceLevel> levelPool2{64};
    FlatHashMap<uint64_t, OrderNode *, kOrderMapSize> orderMap2;
    OrderBook<kFHMSize, kSkipListMaxLevel, kOrderMapSize, std::greater<int64_t>>
        bid{32, nodePool2, levelPool2, orderMap2};

    auto add = [&](uint64_t id, uint32_t amount, int64_t price) {
        OrderNode *n = nodePool2.acquire();
        n->SetOrder(MakeOrder(id, amount, price));
        orderMap2.AddNode(id, n);
        (void)bid.AddLimitOrder(n);
    };

    add(1, 10, 95);
    add(2, 10, 100);
    add(3, 10, 105);

    EXPECT_EQ(bid.GetBestLevel()->GetKey(), 105);
}
