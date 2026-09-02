#include <gtest/gtest.h>
#include <order.hpp>
#include <order_book_update.hpp>
#include <order_state_report.hpp>

#include <cstddef>

using naoto::Order;
using naoto::OrderAction;
using naoto::OrderBookUpdate;
using naoto::OrderSide;
using naoto::OrderState;
using naoto::OrderStateReport;
using naoto::OrderType;

// These wire structs are sent as raw bytes over UDP multicast / TCP (see
// the project notes: "order binaries are naturally aligned while being
// packed, to send it on wire, while handling it without misalignement
// overhead"). A size regression here silently breaks the wire protocol
// between services built from different translation units, so pin the
// expected sizes down explicitly rather than relying on someone noticing
// a mismatch at runtime.

TEST(OrderWireLayoutTest, OrderSizeIsStable)
{
    // Price(8) + Timestamp(8) + OrderId(8) + ClientOrderId(4) +
    // ClientId(4) + Amount(4) + AssetId(2) + Type(1) + Side(1) +
    // Action(1) + Padding(7) = 48, and the struct is #pragma pack(1)'d.
    EXPECT_EQ(sizeof(Order), 48u);
}

TEST(OrderWireLayoutTest, OrderFieldOffsetsAreNaturallyAlignedWithinThePackedStruct)
{
    // "Naturally aligned while packed" means every field still sits at
    // an offset that's a multiple of its own size, so no unaligned
    // load/store is needed to read it back off the wire even though the
    // struct as a whole has no compiler-inserted padding.
    EXPECT_EQ(offsetof(Order, Price) % alignof(int64_t), 0u);
    EXPECT_EQ(offsetof(Order, Timestamp) % alignof(uint64_t), 0u);
    EXPECT_EQ(offsetof(Order, OrderId) % alignof(uint64_t), 0u);
    EXPECT_EQ(offsetof(Order, ClientOrderId) % alignof(uint32_t), 0u);
    EXPECT_EQ(offsetof(Order, ClientId) % alignof(uint32_t), 0u);
    EXPECT_EQ(offsetof(Order, Amount) % alignof(uint32_t), 0u);
    EXPECT_EQ(offsetof(Order, AssetId) % alignof(uint16_t), 0u);
}

TEST(OrderWireLayoutTest, OrderStateReportSizeIsStable)
{
    // 3x int64_t (24) + SequenceId/ClientId/OrderId/TradeId (4x4=16) +
    // BoughtAssetId/SoldAssetId (2x2=4) + State (uint32_t, 4) = 48,
    // pack(1).
    EXPECT_EQ(sizeof(OrderStateReport), 48u);
}

TEST(OrderWireLayoutTest, OrderBookUpdateSizeIsStable)
{
    // SequenceId(4) + Depth(4) + Price(8) + AssetId(2) + Side(1) = 19,
    // pack(1).
    EXPECT_EQ(sizeof(OrderBookUpdate), 19u);
}

TEST(OrderFillReportTest, FillReportSetsAllFields)
{
    OrderStateReport report{};
    report.FillReport(/*boughtDelta=*/1, /*soldDelta=*/-2,
                       /*soldAttemptDelta=*/-3, /*sequenceId=*/4,
                       /*clientId=*/5, /*orderId=*/6, /*tradeId=*/7,
                       /*boughtAssetId=*/8, /*soldAssetId=*/9,
                       OrderState::FILL);

    EXPECT_EQ(report.BoughtDelta, 1);
    EXPECT_EQ(report.SoldDelta, -2);
    EXPECT_EQ(report.SoldAttemptDelta, -3);
    EXPECT_EQ(report.SequenceId, 4u);
    EXPECT_EQ(report.ClientId, 5u);
    EXPECT_EQ(report.OrderId, 6u);
    EXPECT_EQ(report.TradeId, 7u);
    EXPECT_EQ(report.BoughtAssetId, 8u);
    EXPECT_EQ(report.SoldAssetId, 9u);
    EXPECT_EQ(report.State, OrderState::FILL);
}

TEST(OrderBookUpdateTest, FillUpdateSetsAllFields)
{
    OrderBookUpdate update{};
    update.FillUpdate(/*sequenceId=*/1, /*depth=*/2, /*price=*/3,
                       /*assetId=*/4, ORDER_BOOK_UPDATE_SELL);

    EXPECT_EQ(update.SequenceId, 1u);
    EXPECT_EQ(update.Depth, 2u);
    EXPECT_EQ(update.Price, 3);
    EXPECT_EQ(update.AssetId, 4u);
    EXPECT_EQ(update.Side, ORDER_BOOK_UPDATE_SELL);
}

TEST(OrderLogTest, LogDoesNotCrashForEitherEnumBranch)
{
    Order buyOrder{};
    buyOrder.Side = OrderSide::BUY;
    buyOrder.Type = OrderType::LIMIT;
    buyOrder.Action = OrderAction::EXECUTE;
    EXPECT_NO_FATAL_FAILURE(buyOrder.log());

    Order sellOrder{};
    sellOrder.Side = OrderSide::SELL;
    sellOrder.Type = OrderType::MARKET;
    sellOrder.Action = OrderAction::CANCEL;
    EXPECT_NO_FATAL_FAILURE(sellOrder.log());
}
