#include <gtest/gtest.h>
#include <order.hpp>
#include <order_book.hpp>
#include <order_node.hpp>
#include <single_threaded_storage_pool.hpp>

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

    class OrderBookTest : public ::testing::Test
    {
    protected:
        SingleThreadedStoragePool<OrderNode> nodePool{64};
        SingleThreadedStoragePool<PriceLevel> levelPool{64};
        OrderBook<kFHMSize, kSkipListMaxLevel> book{32, nodePool, levelPool};

        OrderNode *AddLimit(uint64_t orderId, uint32_t amount, int64_t price)
        {
            OrderNode *n = nodePool.acquire();
            n->SetOrder(MakeOrder(orderId, amount, price));
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
}

TEST_F(OrderBookTest, DeleteLastOrderAtLevelRemovesTheLevelEntirely)
{
    OrderNode *n1 = AddLimit(1, 10, 100);

    PriceLevel *result = book.DeleteOrder(n1);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(book.GetBestLevel(), nullptr);
    EXPECT_EQ(book.GetBestOffer(), nullptr);
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
    OrderBook<kFHMSize, kSkipListMaxLevel, std::greater<int64_t>> bid{
        32, nodePool2, levelPool2};

    auto add = [&](uint64_t id, uint32_t amount, int64_t price) {
        OrderNode *n = nodePool2.acquire();
        n->SetOrder(MakeOrder(id, amount, price));
        (void)bid.AddLimitOrder(n);
    };

    add(1, 10, 95);
    add(2, 10, 100);
    add(3, 10, 105);

    EXPECT_EQ(bid.GetBestLevel()->GetKey(), 105);
}
