#pragma once

#include <cstdint>
#include <flat_hash_map.hpp>
#include <gtest/gtest_prod.h>
#include <object_batch.hpp>
#include <order.hpp>
#include <order_book.hpp>
#include <order_book_update.hpp>
#include <order_node.hpp>
#include <order_state_report.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>
#include <string>
#include <thread>
#include <vector>

namespace naoto::matching_engine
{
    template <size_t FHMSize, size_t SkipListMaxLevel, size_t BatchSize,
              size_t OrderMapSize>
    class BidAsk
    {
        FRIEND_TEST(BidAskTest, MarketableBuyCrossesRestingAsk);
        FRIEND_TEST(BidAskTest, NonMarketableBuyRestsInBook);

        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ObjectBatch<Order, BatchSize> *>;
        using OrderStatesQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;
        using OrderBookUpdatesQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderBookUpdate *>;

    private:
        uint32_t MarketAssetId;
        int64_t MarketPrice;
        int64_t BestAskPrice;
        int64_t BestBidPrice;
        uint32_t ReportSequenceId;
        uint32_t OrderBookSequenceId;

        OrdersQueue &IncomingOrders;
        OrderStatesQueue &OutgoingOrders;
        OrderBookUpdatesQueue &OutgoingBook;
        StoragePool<ObjectBatch<Order, BatchSize>> &OrdersPool;
        StoragePool<OrderStateReport> &OrderReportsPool;
        StoragePool<OrderBookUpdate> &OrderBookUpdatesPool;
        UnsafeStoragePool<OrderNode> OrderNodePool;
        UnsafeStoragePool<PriceLevel> PriceLevelPool;

        FlatHashMap<uint64_t, OrderNode *, OrderMapSize> OrderMap;

        OrderBook<FHMSize, SkipListMaxLevel, std::greater<int64_t>> Bid;
        OrderBook<FHMSize, SkipListMaxLevel> Ask;

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

            OrderNode *toCancel;
            bool found = OrderMap.GetVal(order.Amount, toCancel);

            if (!found || toCancel->GetClientId() != order.Amount) [[unlikely]]
            {
                OrderStateReport *rejectReport = OrderReportsPool.acquire();
                rejectReport->FillReport(0, 0, 0, ReportSequenceId++,
                                         order.ClientId, order.OrderId, 0, 0, 0,
                                         OrderState::REJECT);
                OutgoingOrders.try_enqueue(rejectReport);

                std::cout << "Rejected a cancel request.\n\n";

                return;
            }

            int64_t curPrice = toCancel->GetPrice();

            if constexpr (side == OrderSide::BUY)
            {
                // TODO : make a wrapper function for this operation (think
                // about if it hurts performances, might enforce inline because
                // of large number of params)
                OrderStateReport *cancelReport = OrderReportsPool.acquire();
                cancelReport->FillReport(
                    0, 0, toCancel->GetAmount() * toCancel->GetPrice(),
                    ReportSequenceId++, toCancel->GetClientId(),
                    toCancel->GetId(), 0, MarketAssetId, 0, OrderState::CANCEL);
                OutgoingOrders.try_enqueue(cancelReport);

                PriceLevel *curLevel = Bid.DeleteOrder(toCancel);

                OrderBookUpdate *curOrderBookUpdate =
                    OrderBookUpdatesPool.acquire();

                curOrderBookUpdate->FillUpdate(
                    OrderBookSequenceId++,
                    curLevel ? curLevel->GetTotalAmount() : 0, curPrice,
                    MarketAssetId, ORDER_BOOK_UPDATE_BUY);

                OutgoingBook.try_enqueue(curOrderBookUpdate);
            }
            else
            {
                OrderStateReport *cancelReport = OrderReportsPool.acquire();
                cancelReport->FillReport(
                    0, 0, toCancel->GetAmount(), ReportSequenceId++,
                    toCancel->GetClientId(), toCancel->GetId(), 0, 0,
                    MarketAssetId, OrderState::CANCEL);
                OutgoingOrders.try_enqueue(cancelReport);

                PriceLevel *curLevel = Ask.DeleteOrder(toCancel);

                OrderBookUpdate *curOrderBookUpdate =
                    OrderBookUpdatesPool.acquire();

                curOrderBookUpdate->FillUpdate(
                    OrderBookSequenceId++,
                    curLevel ? curLevel->GetTotalAmount() : 0, curPrice,
                    MarketAssetId, ORDER_BOOK_UPDATE_SELL);

                OutgoingBook.try_enqueue(curOrderBookUpdate);
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
                    std::cout << "Matching the following orders:\n";
                    order.log();
                    bestOffer->log();

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

                    OrderStateReport *orderReport = OrderReportsPool.acquire();
                    orderReport->FillReport(
                        tradedAmount, soldDelta * BestAskPrice,
                        soldDelta * BestAskPrice, ReportSequenceId++,
                        order.ClientId, order.OrderId, 0, MarketAssetId, 0,
                        order.Amount > 0 ? OrderState::PARTIAL_FILL
                                         : OrderState::FILL);
                    OutgoingOrders.try_enqueue(orderReport);

                    OrderStateReport *offerReport = OrderReportsPool.acquire();
                    offerReport->FillReport(
                        tradedAmount * BestAskPrice, soldDelta, soldDelta,
                        ReportSequenceId++, bestOffer->GetClientId(),
                        bestOffer->GetId(), 0, 0, MarketAssetId,
                        bestOffer->GetAmount() > 0 ? OrderState::PARTIAL_FILL
                                                   : OrderState::FILL);
                    OutgoingOrders.try_enqueue(offerReport);

                    OrderBookUpdate *curOrderBookUpdate =
                        OrderBookUpdatesPool.acquire();
                    curOrderBookUpdate->FillUpdate(
                        OrderBookSequenceId++, bestLevel->GetTotalAmount(),
                        BestAskPrice, MarketAssetId, ORDER_BOOK_UPDATE_SELL);
                    OutgoingBook.try_enqueue(curOrderBookUpdate);

                    std::cout << "Orders after matching\n:";
                    order.log();
                    bestOffer->log();

                    std::cout << "New market price:\n" << MarketPrice << "\n";

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
                OrderStateReport *orderReport = OrderReportsPool.acquire();
                orderReport->FillReport(0, 0, -totalLocked, ReportSequenceId++,
                                        order.ClientId, order.OrderId, 0,
                                        MarketAssetId, 0, OrderState::CANCEL);
                OutgoingOrders.try_enqueue(orderReport);
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
                    std::cout << "Matching the following orders:\n";
                    order.log();
                    bestOffer->log();

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

                    OrderStateReport *orderReport = OrderReportsPool.acquire();
                    orderReport->FillReport(
                        tradedAmount * BestBidPrice, soldDelta, soldDelta,
                        ReportSequenceId++, order.ClientId, order.OrderId, 0, 0,
                        MarketAssetId,
                        order.Amount > 0 ? OrderState::PARTIAL_FILL
                                         : OrderState::FILL);
                    OutgoingOrders.try_enqueue(orderReport);
                    // TODO : try_enqueue failure handling, should loop until it
                    // works

                    OrderStateReport *offerReport = OrderReportsPool.acquire();
                    offerReport->FillReport(
                        tradedAmount, soldDelta * BestBidPrice,
                        soldDelta * BestBidPrice, ReportSequenceId++,
                        bestOffer->GetClientId(), bestOffer->GetId(), 0,
                        MarketAssetId, 0,
                        bestOffer->GetAmount() > 0 ? OrderState::PARTIAL_FILL
                                                   : OrderState::FILL);
                    OutgoingOrders.try_enqueue(offerReport);

                    OrderBookUpdate *curOrderBookUpdate =
                        OrderBookUpdatesPool.acquire();
                    curOrderBookUpdate->FillUpdate(
                        OrderBookSequenceId++, bestLevel->GetTotalAmount(),
                        BestBidPrice, MarketAssetId, ORDER_BOOK_UPDATE_BUY);
                    OutgoingBook.try_enqueue(curOrderBookUpdate);

                    std::cout << "Orders after matching\n:";
                    order.log();
                    bestOffer->log();

                    std::cout << "New market price:\n" << MarketPrice << "\n";

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
                OrderStateReport *orderReport = OrderReportsPool.acquire();
                orderReport->FillReport(0, 0, -totalLocked, ReportSequenceId++,
                                        order.ClientId, order.OrderId, 0, 0,
                                        MarketAssetId, OrderState::CANCEL);
                OutgoingOrders.try_enqueue(orderReport);
            }
        }

        void ExecuteMarketableOrder(
            Order &order) noexcept // assumption: the order IS marketable
        {
            std::cout << "Executing marketable order.\n\n";
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
            OrderNode *toAdd = OrderNodePool.acquire();
            toAdd->SetOrder(order);

            BestAskPrice = std::min(BestAskPrice, order.Price);

            OrderMap.AddNode(toAdd->GetId(), toAdd);

            PriceLevel *curLevel = Ask.AddLimitOrder(toAdd);

            OrderStateReport *addReport = OrderReportsPool.acquire();
            addReport->FillReport(0, 0, order.Amount, ReportSequenceId++,
                                  order.ClientId, order.OrderId, 0, 0,
                                  MarketAssetId, OrderState::ADD);
            OutgoingOrders.try_enqueue(addReport);
            // TODO : try_enqueue failure handling

            OrderBookUpdate *curOrderBookUpdate =
                OrderBookUpdatesPool.acquire();
            curOrderBookUpdate->FillUpdate(
                OrderBookSequenceId++, curLevel->GetTotalAmount(),
                curLevel->GetKey(), MarketAssetId, ORDER_BOOK_UPDATE_SELL);
            OutgoingBook.try_enqueue(curOrderBookUpdate);
        }

        void AddBuyLimitOrder(Order &order)
        {
            OrderNode *toAdd = OrderNodePool.acquire();

            if (!toAdd) [[unlikely]]
            {
                std::cout << "Matching engine failed getting an order node "
                             "from pool.\n\n";
            }

            toAdd->SetOrder(order);

            BestBidPrice = std::max(BestBidPrice, order.Price);

            OrderMap.AddNode(toAdd->GetId(), toAdd);

            PriceLevel *curLevel = Bid.AddLimitOrder(toAdd);

            OrderStateReport *addReport = OrderReportsPool.acquire();
            addReport->FillReport(0, 0, order.Amount * order.Price,
                                  ReportSequenceId++, order.ClientId,
                                  order.OrderId, 0, MarketAssetId, 0,
                                  OrderState::ADD);
            OutgoingOrders.try_enqueue(addReport);
            // TODO : try_enqueue failure handling

            OrderBookUpdate *curOrderBookUpdate =
                OrderBookUpdatesPool.acquire();
            curOrderBookUpdate->FillUpdate(
                OrderBookSequenceId++, curLevel->GetTotalAmount(),
                curLevel->GetKey(), MarketAssetId, ORDER_BOOK_UPDATE_BUY);
            OutgoingBook.try_enqueue(curOrderBookUpdate);
            // TODO : failure handling
        }

        void AddLimitOrder(
            Order &order) noexcept // assumption: the order is not marketable
        {
            std::cout << "Adding to order book\n";
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
               OrdersQueue &incoming, OrderStatesQueue &outgoingOrders,
               OrderBookUpdatesQueue &outgoingBook,
               StoragePool<ObjectBatch<Order, BatchSize>> &pool,
               StoragePool<OrderStateReport> &orderReportsPool,
               StoragePool<OrderBookUpdate> &orderBookUpdatesPool,
               const size_t orderNodePoolSize,
               const size_t skipListNodesPoolSize)
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
            , OrdersPool(pool)
            , OrderReportsPool(orderReportsPool)
            , OrderBookUpdatesPool(orderBookUpdatesPool)
            , OrderNodePool(orderNodePoolSize)
            , PriceLevelPool(orderNodePoolSize)
            , Bid(skipListNodesPoolSize, OrderNodePool, PriceLevelPool)
            , Ask(skipListNodesPoolSize, OrderNodePool, PriceLevelPool)
        {}

        ~BidAsk() = default;

        void MarketExecutionLoop() noexcept
        {
            while (true)
            {
                ObjectBatch<Order, BatchSize> *batch = nullptr;

                if (IncomingOrders.try_dequeue(batch)) [[likely]]
                {
                    std::cout << "batch of size " << batch->getSize() << "\n";

                    for (size_t i = 0; i < batch->getSize(); ++i)
                    {
                        Order &curOrder = batch->Data[i];

                        std::cout << "Received the order:\n";
                        curOrder.log();

                        if (curOrder.Action == OrderAction::EXECUTE)
                        {
                            executeOrder(curOrder);
                            std::cout << "finished execution\n";
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

                    bool released = OrdersPool.release(batch);

                    if (!released) [[unlikely]]
                    {
                        std::cerr << "failed to release in execution loop\n";
                        std::terminate();
                    }
                }
            }
        }
    };
} // namespace naoto::matching_engine
