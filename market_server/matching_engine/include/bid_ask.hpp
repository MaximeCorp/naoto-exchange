#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <flat_hash_map.hpp>
#include <functional>
#include <gtest/gtest_prod.h>
#include <matching_engine_types.hpp>
#include <object_batch.hpp>
#include <order.hpp>
#include <order_book.hpp>
#include <order_book_update.hpp>
#include <order_node.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <shared_memory_types.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <string>
#include <system_conf.hpp>
#include <thread>
#include <vector>

#ifdef NAOTO_PERF
#    include <timestamps.hpp>
#endif

namespace naoto::matching_engine
{
    class BidAsk
    {
        FRIEND_TEST(BidAskTest, MarketableBuyCrossesRestingAsk);
        FRIEND_TEST(BidAskTest, NonMarketableBuyRestsInBook);
        // Additional hooks added while building out the GoogleTest suite
        // (tests/market_server/unit/test_bid_ask.cpp) -- same pattern as the
        // two above, granting the named TEST_F(BidAskTest, ...) cases access to
        // executeOrder()/CancelOrder<>()/the private Bid/Ask/OrderMap
        // members so matching behavior can be driven and inspected
        // directly instead of only through the blocking
        // MarketExecutionLoop().
        FRIEND_TEST(BidAskTest, NonMarketableSellRestsInBook);
        FRIEND_TEST(BidAskTest, MarketableSellCrossesRestingBid);
        FRIEND_TEST(BidAskTest, PriceTimePriorityFifoAtSameLevel);
        FRIEND_TEST(BidAskTest, MarketOrderSweepsMultiplePriceLevels);
        FRIEND_TEST(BidAskTest, MarketOrderWithNoLiquidityIsFullyCancelled);
        FRIEND_TEST(BidAskTest,
                    MarketableLimitBuyPartialFillCancelsRemainder_LIKELY_BUG);
        FRIEND_TEST(BidAskTest, CancelOrderRemovesRestingBuyFromBook);
        FRIEND_TEST(BidAskTest, CancelOrderRemovesRestingSellFromBook);
        FRIEND_TEST(BidAskTest, CancelOrderRejectedForUnknownOrderId);
        FRIEND_TEST(BidAskTest, CancelOrderRejectedForWrongClientId);
        FRIEND_TEST(BidAskTest, ExactFillRemovesRestingOrderFromBook);
        FRIEND_TEST(BidAskTest,
                    MarketBuyOrderWithZeroPriceNeverMatches_LIKELY_BUG);

    private:
        uint32_t MarketAssetId;
        int64_t MarketPrice;
        int64_t BestAskPrice;
        int64_t BestBidPrice;
        uint32_t ReportSequenceId;
        uint32_t OrderBookSequenceId;

#ifdef NAOTO_SHARED_MEMORY
        OrderConsumer IncomingOrders;
#else
        OrderBatchConsumer IncomingOrders;
#endif
        OrderStateProducer OutgoingOrders;
        OrderBookUpdateProducer OutgoingBook;
#ifndef NAOTO_SHARED_MEMORY
        OrderBatchMempool &OrdersPool;
#endif
        OrderStateMempool &OrderReportsPool;
        OrderBookUpdateMempool &OrderBookUpdatesPool;
        OrderNodeMempool OrderNodePool;
        PriceLevelMempool PriceLevelPool;

        OrderIdMap OrderMap;

        OrderBook<std::greater<int64_t>> Bid;
        OrderBook<> Ask;

        [[nodiscard]] bool IsMarketable(Order &order) const noexcept
        {
            bool is_market = (order.Type == OrderType::MARKET);
            bool is_buy = (order.Side == OrderSide::BUY);

            std::int64_t price = order.Price;

            bool limit_marketable =
                is_buy ? (price >= BestAskPrice) : (price <= BestBidPrice);

            return is_market || limit_marketable;
        }

        template <OrderSide side>
        void CancelOrder(Order &order) noexcept
        {
            // order.Amount field is reused for cancel requests to represent the
            // id of the order that we want to cancel

            OrderNode *toCancel = nullptr;
            bool found = OrderMap.GetVal(order.Amount, toCancel);

#ifdef NAOTO_PERF
            uint64_t now = now_tsc();
#endif
            if (!found || toCancel->GetClientId() != order.ClientId)
                [[unlikely]]
            {
                OrderStateReport *rejectReport = OrderReportsPool.Acquire();
                rejectReport->FillReport(
                    0, 0, 0,

#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, order.ClientId, order.OrderId, 0, 0, 0,
                    OrderState::REJECT);
                (void)OutgoingOrders.TryPush(rejectReport);

                return;
            }

            int64_t curPrice = toCancel->GetPrice();

            if constexpr (side == OrderSide::BUY)
            {
                // TODO : make a wrapper function for this operation (think
                // about if it hurts performances, might enforce inline because
                // of large number of params)
                OrderStateReport *cancelReport = OrderReportsPool.Acquire();
                cancelReport->FillReport(
                    0, 0, toCancel->GetAmount() * toCancel->GetPrice(),

#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, toCancel->GetClientId(),
                    toCancel->GetId(), 0, MarketAssetId, 0, OrderState::CANCEL);
                OutgoingOrders.Push(cancelReport);

                std::cout << "Successful cancel\n\n";

                PriceLevel *curLevel = Bid.DeleteOrder(toCancel);

                OrderBookUpdate *curOrderBookUpdate =
                    OrderBookUpdatesPool.Acquire();

                curOrderBookUpdate->FillUpdate(

#ifdef NAOTO_PERF
                    now,
#endif
                    OrderBookSequenceId++,
                    curLevel ? curLevel->GetTotalAmount() : 0, curPrice,
                    MarketAssetId, ORDER_BOOK_UPDATE_BUY);

                OutgoingBook.Push(curOrderBookUpdate);
            }
            else
            {
                OrderStateReport *cancelReport = OrderReportsPool.Acquire();
                cancelReport->FillReport(
                    0, 0, toCancel->GetAmount(),

#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, toCancel->GetClientId(),
                    toCancel->GetId(), 0, 0, MarketAssetId, OrderState::CANCEL);
                OutgoingOrders.Push(cancelReport);

                std::cout << "Successful cancel\n\n";

                PriceLevel *curLevel = Ask.DeleteOrder(toCancel);

                OrderBookUpdate *curOrderBookUpdate =
                    OrderBookUpdatesPool.Acquire();

                curOrderBookUpdate->FillUpdate(

#ifdef NAOTO_PERF
                    now,
#endif
                    OrderBookSequenceId++,
                    curLevel ? curLevel->GetTotalAmount() : 0, curPrice,
                    MarketAssetId, ORDER_BOOK_UPDATE_SELL);

                OutgoingBook.Push(curOrderBookUpdate);
            }
        }

        void FillBuyOrder(Order &order) noexcept
        {
            int64_t totalLocked = order.Amount * order.Price;

            while (order.Amount > 0 && IsMarketable(order))
            {
                PriceLevel *bestLevel = Ask.GetBestLevel();

                if (!bestLevel) [[unlikely]]
                {
                    break;
                }

                OrderNode *bestOffer = bestLevel->PeekOrder();

                if (!bestOffer) [[unlikely]]
                {
                    break;
                }

                BestAskPrice = bestLevel->GetKey();

                if (BestAskPrice > order.Price) [[unlikely]]
                {
                    break;
                }

                while (order.Amount > 0 && IsMarketable(order) && bestOffer)
                {
                    const std::uint32_t curOrderAmount = order.Amount;
                    const std::uint32_t curOfferAmount = bestOffer->GetAmount();

                    const uint32_t tradedAmount =
                        (curOrderAmount < curOfferAmount) ? curOrderAmount
                                                          : curOfferAmount;

                    const std::uint32_t orderAmount =
                        curOrderAmount - tradedAmount;
                    const std::uint32_t offerAmount =
                        curOfferAmount - tradedAmount;

                    MarketPrice =
                        tradedAmount > 0 ? bestOffer->GetPrice() : MarketPrice;

                    order.Amount = orderAmount;
                    bestOffer->SetAmount(offerAmount);
                    bestLevel->IncTotalAmount(-tradedAmount);

                    int64_t soldDelta = -static_cast<int64_t>(tradedAmount);

                    totalLocked += soldDelta * BestAskPrice;

#ifdef NAOTO_PERF
                    uint64_t now = now_tsc();
#endif

                    OrderStateReport *orderReport = OrderReportsPool.Acquire();
                    orderReport->FillReport(
                        tradedAmount, soldDelta * BestAskPrice,
                        soldDelta * BestAskPrice,

#ifdef NAOTO_PERF
                        order.IngestedTimestamp, order.RoutedTimestamp,
                        order.ReceivedTimestamp, now,
#endif
                        ReportSequenceId++, order.ClientId, order.OrderId, 0,
                        MarketAssetId, 0,
                        order.Amount > 0 ? OrderState::PARTIAL_FILL
                                         : OrderState::FILL);
                    (void)OutgoingOrders.TryPush(orderReport);

                    OrderStateReport *offerReport = OrderReportsPool.Acquire();
                    offerReport->FillReport(
                        tradedAmount * BestAskPrice, soldDelta, soldDelta,

#ifdef NAOTO_PERF
                        bestOffer->GetIngested(), bestOffer->GetRouted(),
                        bestOffer->GetReceived(), now,
#endif
                        ReportSequenceId++, bestOffer->GetClientId(),
                        bestOffer->GetId(), 0, 0, MarketAssetId,
                        bestOffer->GetAmount() > 0 ? OrderState::PARTIAL_FILL
                                                   : OrderState::FILL);
                    (void)OutgoingOrders.TryPush(offerReport);

                    OrderBookUpdate *curOrderBookUpdate =
                        OrderBookUpdatesPool.Acquire();
                    curOrderBookUpdate->FillUpdate(

#ifdef NAOTO_PERF
                        now,
#endif
                        OrderBookSequenceId++, bestLevel->GetTotalAmount(),
                        BestAskPrice, MarketAssetId, ORDER_BOOK_UPDATE_SELL);
                    (void)OutgoingBook.TryPush(curOrderBookUpdate);

                    if (bestOffer->GetAmount() == 0)
                    {
                        OrderNode *toDelete = bestOffer;
                        bestOffer = bestOffer->GetNext();
                        Ask.DeleteOrder(toDelete);
                    }
                }
            }

            if (order.Amount > 0) [[unlikely]]
            {
#ifdef NAOTO_PERF
                uint64_t now = now_tsc();
#endif

                OrderStateReport *orderReport = OrderReportsPool.Acquire();
                orderReport->FillReport(
                    0, 0, -totalLocked,
#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, order.ClientId, order.OrderId, 0,
                    MarketAssetId, 0, OrderState::CANCEL);
                OutgoingOrders.Push(orderReport);

                std::cout << "No matches found\n\n";
            }
        }

        void FillSellOrder(Order &order) noexcept
        {
            int64_t totalLocked = order.Amount;

            while (order.Amount > 0 && IsMarketable(order))
            {
                PriceLevel *bestLevel = Bid.GetBestLevel();

                if (!bestLevel) [[unlikely]]
                {
                    break;
                }

                OrderNode *bestOffer = bestLevel->PeekOrder();

                if (!bestOffer) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    break;
                }

                BestBidPrice = bestLevel->GetKey();

                if (BestBidPrice < order.Price) [[unlikely]]
                {
                    break;
                }

                while (order.Amount > 0 && IsMarketable(order) && bestOffer)
                {
                    const std::uint32_t curOrderAmount = order.Amount;
                    const std::uint32_t curOfferAmount = bestOffer->GetAmount();

                    const uint32_t tradedAmount =
                        (curOrderAmount < curOfferAmount) ? curOrderAmount
                                                          : curOfferAmount;

                    const std::uint32_t orderAmount =
                        curOrderAmount - tradedAmount;
                    const std::uint32_t offerAmount =
                        curOfferAmount - tradedAmount;

                    MarketPrice =
                        tradedAmount > 0 ? bestOffer->GetPrice() : MarketPrice;

                    order.Amount = orderAmount;
                    bestOffer->SetAmount(offerAmount);
                    bestLevel->IncTotalAmount(-tradedAmount);

                    int64_t soldDelta = -static_cast<int64_t>(tradedAmount);
                    totalLocked += soldDelta;

#ifdef NAOTO_PERF
                    uint64_t now = now_tsc();
#endif

                    OrderStateReport *orderReport = OrderReportsPool.Acquire();
                    orderReport->FillReport(
                        tradedAmount * BestBidPrice, soldDelta, soldDelta,
#ifdef NAOTO_PERF
                        order.IngestedTimestamp, order.RoutedTimestamp,
                        order.ReceivedTimestamp, now,
#endif
                        ReportSequenceId++, order.ClientId, order.OrderId, 0, 0,
                        MarketAssetId,
                        order.Amount > 0 ? OrderState::PARTIAL_FILL
                                         : OrderState::FILL);
                    (void)OutgoingOrders.TryPush(orderReport);
                    // TODO : try_enqueue failure handling, should loop until it
                    // works

                    OrderStateReport *offerReport = OrderReportsPool.Acquire();
                    offerReport->FillReport(
                        tradedAmount, soldDelta * BestBidPrice,
                        soldDelta * BestBidPrice,
#ifdef NAOTO_PERF
                        bestOffer->GetIngested(), bestOffer->GetRouted(),
                        bestOffer->GetReceived(), now,
#endif
                        ReportSequenceId++, bestOffer->GetClientId(),
                        bestOffer->GetId(), 0, MarketAssetId, 0,
                        bestOffer->GetAmount() > 0 ? OrderState::PARTIAL_FILL
                                                   : OrderState::FILL);
                    (void)OutgoingOrders.TryPush(offerReport);

                    OrderBookUpdate *curOrderBookUpdate =
                        OrderBookUpdatesPool.Acquire();
                    curOrderBookUpdate->FillUpdate(
#ifdef NAOTO_PERF
                        now,
#endif
                        OrderBookSequenceId++, bestLevel->GetTotalAmount(),
                        BestBidPrice, MarketAssetId, ORDER_BOOK_UPDATE_BUY);
                    (void)OutgoingBook.TryPush(curOrderBookUpdate);

                    if (bestOffer->GetAmount() == 0)
                    {
                        OrderNode *toDelete = bestOffer;
                        bestOffer = bestOffer->GetNext();
                        Bid.DeleteOrder(toDelete);
                    }
                }
            }

            if (order.Amount > 0) [[unlikely]]
            {
#ifdef NAOTO_PERF
                uint64_t now = now_tsc();
#endif

                OrderStateReport *orderReport = OrderReportsPool.Acquire();
                orderReport->FillReport(
                    0, 0, -totalLocked,
#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, order.ClientId, order.OrderId, 0, 0,
                    MarketAssetId, OrderState::CANCEL);
                OutgoingOrders.Push(orderReport);

                std::cout << "No matches found\n\n";
            }
        }

        void ExecuteMarketableOrder(
            Order &order) noexcept // assumption: the order IS marketable
        {
            if (order.Side == OrderSide::BUY)
            {
                FillBuyOrder(order);
            }
            else
            {
                FillSellOrder(order);
            }
        }

        void AddSellLimitOrder(Order &order)
        {
            OrderNode *toAdd = OrderNodePool.Acquire();

            if (!toAdd) [[unlikely]]
            {
                return;
            }

            toAdd->SetOrder(order);

            BestAskPrice = std::min(BestAskPrice, order.Price);

            OrderMap.AddNode(toAdd->GetId(), toAdd);

            PriceLevel *curLevel = Ask.AddLimitOrder(toAdd);

            OrderStateReport *addReport = OrderReportsPool.Acquire();

#ifdef NAOTO_PERF
            uint64_t now = now_tsc();
#endif

            if (addReport) [[likely]]
            {
                addReport->FillReport(
                    0, 0, order.Amount,
#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, order.ClientId, order.OrderId, 0, 0,
                    MarketAssetId, OrderState::ADD);
                (void)OutgoingOrders.TryPush(addReport);
                // TODO : try_enqueue failure handling
            }

            OrderBookUpdate *curOrderBookUpdate =
                OrderBookUpdatesPool.Acquire();

            if (curOrderBookUpdate) [[likely]]
            {
                curOrderBookUpdate->FillUpdate(
#ifdef NAOTO_PERF
                    now,
#endif
                    OrderBookSequenceId++, curLevel->GetTotalAmount(),
                    curLevel->GetKey(), MarketAssetId, ORDER_BOOK_UPDATE_SELL);
                (void)OutgoingBook.TryPush(curOrderBookUpdate);
            }
        }

        void AddBuyLimitOrder(Order &order)
        {
            OrderNode *toAdd = OrderNodePool.Acquire();

            if (!toAdd) [[unlikely]]
            {
                return;
            }

            toAdd->SetOrder(order);

            BestBidPrice = std::max(BestBidPrice, order.Price);

            OrderMap.AddNode(toAdd->GetId(), toAdd);

            PriceLevel *curLevel = Bid.AddLimitOrder(toAdd);

            OrderStateReport *addReport = OrderReportsPool.Acquire();

#ifdef NAOTO_PERF
            uint64_t now = now_tsc();
#endif

            if (addReport) [[likely]]
            {
                addReport->FillReport(
                    0, 0, order.Amount * order.Price,
#ifdef NAOTO_PERF
                    order.IngestedTimestamp, order.RoutedTimestamp,
                    order.ReceivedTimestamp, now,
#endif
                    ReportSequenceId++, order.ClientId, order.OrderId, 0,
                    MarketAssetId, 0, OrderState::ADD);
                (void)OutgoingOrders.TryPush(addReport);
                // TODO : try_enqueue failure handling
            }

            OrderBookUpdate *curOrderBookUpdate =
                OrderBookUpdatesPool.Acquire();

            if (curOrderBookUpdate) [[likely]]
            {
                curOrderBookUpdate->FillUpdate(
#ifdef NAOTO_PERF
                    now,
#endif
                    OrderBookSequenceId++, curLevel->GetTotalAmount(),
                    curLevel->GetKey(), MarketAssetId, ORDER_BOOK_UPDATE_BUY);
                (void)OutgoingBook.TryPush(curOrderBookUpdate);
            }
            // TODO : failure handling
        }

        void AddLimitOrder(
            Order &order) noexcept // assumption: the order is not marketable
        {
            const bool isBuy = order.Side == OrderSide::BUY;

            if (isBuy)
            {
                AddBuyLimitOrder(order);
            }
            else
            {
                AddSellLimitOrder(order);
            }
        }

        void executeOrder(Order &order) noexcept
        {
            if (IsMarketable(order))
            {
                ExecuteMarketableOrder(order);
            }
            else
            {
                AddLimitOrder(order);
            }
        }

    public:
        BidAsk(const std::uint32_t assetId, const std::int64_t initialPrice,
#ifdef NAOTO_SHARED_MEMORY
               OrderQueue *incoming,
#else
               OrderBatchQueue *incoming,
#endif
               OrderStateQueue *outgoingOrders,
               OrderBookUpdateQueue *outgoingBook,
#ifndef NAOTO_SHARED_MEMORY
               OrderBatchMempool &pool,
#endif
               OrderStateMempool &orderReportsPool,
               OrderBookUpdateMempool &orderBookUpdatesPool)
            : MarketAssetId(assetId)
            , MarketPrice(initialPrice)
            , BestAskPrice(INT64_MAX)
            , BestBidPrice(INT64_MIN)
            , ReportSequenceId(
                  std::stoi(std::getenv("REPORT_SEQUENCE_ID") ?: "0"))
            , OrderBookSequenceId(
                  std::stoi(std::getenv("ORDER_BOOK_SEQUENCE_ID") ?: "0"))
            , IncomingOrders(incoming)
            , OutgoingOrders(outgoingOrders)
            , OutgoingBook(outgoingBook)
#ifndef NAOTO_SHARED_MEMORY
            , OrdersPool(pool)
#endif
            , OrderReportsPool(orderReportsPool)
            , OrderBookUpdatesPool(orderBookUpdatesPool)
            , Bid(OrderNodePool, PriceLevelPool, OrderMap)
            , Ask(OrderNodePool, PriceLevelPool, OrderMap)
        {
#ifdef NAOTO_SHARED_MEMORY
            std::cout << "Got the shared queue: " << (uintptr_t)incoming
                      << "bytes size:" << sizeof(OrderQueue) << "\n\n";
#endif
        }

        ~BidAsk() = default;

        void MarketExecutionLoop() noexcept
        {
            while (true)
            {
#ifdef NAOTO_SHARED_MEMORY
                Order curOrder;
                bool popped = IncomingOrders.TryPop(curOrder);

                if (!popped)
                {
                    continue;
                }

                curOrder.ReceivedTimestamp = now_tsc();

                if (curOrder.Action == OrderAction::EXECUTE)
                {
                    executeOrder(curOrder);
                }
                else if (curOrder.Action == OrderAction::CANCEL) [[likely]]
                {
                    if (curOrder.Side == OrderSide::BUY)
                    {
                        CancelOrder<OrderSide::BUY>(curOrder);
                    }
                    else
                    {
                        CancelOrder<OrderSide::SELL>(curOrder);
                    }
                }
#else
                OrderBatch *batch = nullptr;

                if (IncomingOrders.TryPop(batch)) [[likely]]
                {
                    for (size_t i = 0; i < batch->getSize(); ++i)
                    {
                        Order &curOrder = batch->Data[i];

                        if (curOrder.Action == OrderAction::EXECUTE)
                        {
                            executeOrder(curOrder);
                        }
                        else if (curOrder.Action == OrderAction::CANCEL)
                            [[likely]]
                        {
                            if (curOrder.Side == OrderSide::BUY)
                            {
                                CancelOrder<OrderSide::BUY>(curOrder);
                            }
                            else
                            {
                                CancelOrder<OrderSide::SELL>(curOrder);
                            }
                        }
                    }

                    bool released = OrdersPool.Release(batch);

                    if (!released) [[unlikely]]
                    {
                        std::cerr << "failed to release in execution loop\n";
                        std::terminate();
                    }
                }
#endif
            }
        }
    };
} // namespace naoto::matching_engine
