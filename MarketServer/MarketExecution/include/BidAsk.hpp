#pragma once

#include <Asset.hpp>
#include <FlatHashMap.hpp>
#include <ObjectBatch.hpp>
#include <Order.hpp>
#include <OrderBook.hpp>
#include <OrderBookUpdate.hpp>
#include <OrderNode.hpp>
#include <OrderStateReport.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstdint>
#include <gtest/gtest_prod.h>
#include <thread>
#include <vector>

namespace MarketExecution
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
            bool is_market = (order.getType() == OrderType::MARKET);
            bool is_buy = (order.getSide() == OrderSide::BUY);

            std::int64_t price = order.getPrice();

            bool limit_marketable =
                is_buy ? (price >= BestAskPrice) : (price <= BestBidPrice);

            return is_market || limit_marketable;
        }

        template <OrderSide side>
        void CancelOrder(Order &order) noexcept
        {
            OrderNode *toCancel = OrderMap.GetVal(order.getPrice());

            if (!toCancel) [[unlikely]]
            {
                return;
            }

            if constexpr (side == OrderSide::BUY)
            {
                Bid.DeleteOrder(toCancel);
            }
            else
            {
                Ask.DeleteOrder(toCancel);
            }
        }

        void FillBuyOrder(Order &order) noexcept
        {
            while (order.getAmount() > 0 && IsMarketable(order))
            {
                PriceLevel *bestLevel = Ask.GetBestLevel();

                if (!bestLevel) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    return;
                }

                OrderNode *bestOffer = bestLevel->PeekOrder();

                if (!bestOffer) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    return;
                }

                BestAskPrice = bestLevel->GetKey();

                while (order.getAmount() > 0 && IsMarketable(order)
                       && bestOffer)
                {
                    std::cout << "Matching the following orders:\n";
                    order.log();
                    bestOffer->log();

                    const std::uint32_t curOrderAmount = order.getAmount();
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

                    order.setAmount(orderAmount);
                    bestOffer->SetAmount(offerAmount);
                    bestLevel->IncTotalAmount(-tradedAmount);

                    OrderStateReport *orderReport = OrderReportsPool.acquire();
                    orderReport->FillReport(tradedAmount,
                                            -static_cast<int64_t>(tradedAmount)
                                                * BestAskPrice,
                                            order.getClientId(), order.getId(),
                                            0, MarketAssetId, 0);
                    OutgoingOrders.try_enqueue(orderReport);

                    OrderStateReport *offerReport = OrderReportsPool.acquire();
                    offerReport->FillReport(tradedAmount * BestAskPrice,
                                            -static_cast<int64_t>(tradedAmount),
                                            bestOffer->GetClientId(),
                                            bestOffer->GetId(), 0, 0,
                                            MarketAssetId);
                    OutgoingOrders.try_enqueue(offerReport);

                    OrderBookUpdate *curOrderBookUpdate =
                        OrderBookUpdatesPool.acquire();
                    curOrderBookUpdate->FillUpdate(bestLevel->GetTotalAmount(),
                                                   BestAskPrice, MarketAssetId,
                                                   ORDER_BOOK_UPDATE_SELL);
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
        }

        void FillSellOrder(Order &order) noexcept
        {
            while (order.getAmount() > 0 && IsMarketable(order))
            {
                PriceLevel *bestLevel = Bid.GetBestLevel();

                if (!bestLevel) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    return;
                }

                OrderNode *bestOffer = bestLevel->PeekOrder();

                if (!bestOffer) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    return;
                }

                BestBidPrice = bestOffer->GetPrice();

                while (order.getAmount() > 0 && IsMarketable(order)
                       && bestOffer)
                {
                    std::cout << "Matching the following orders:\n";
                    order.log();
                    bestOffer->log();

                    const std::uint32_t curOrderAmount = order.getAmount();
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

                    order.setAmount(orderAmount);
                    bestOffer->SetAmount(offerAmount);
                    bestLevel->IncTotalAmount(-tradedAmount);

                    OrderStateReport *orderReport = OrderReportsPool.acquire();
                    orderReport->FillReport(tradedAmount * BestBidPrice,
                                            -static_cast<int64_t>(tradedAmount),
                                            order.getClientId(), order.getId(),
                                            0, 0, MarketAssetId);
                    OutgoingOrders.try_enqueue(orderReport);

                    OrderStateReport *offerReport = OrderReportsPool.acquire();
                    offerReport->FillReport(
                        tradedAmount,
                        -static_cast<int64_t>(tradedAmount) * BestBidPrice,
                        bestOffer->GetClientId(), bestOffer->GetId(), 0,
                        MarketAssetId, 0);
                    OutgoingOrders.try_enqueue(offerReport);

                    OrderBookUpdate *curOrderBookUpdate =
                        OrderBookUpdatesPool.acquire();
                    curOrderBookUpdate->FillUpdate(bestLevel->GetTotalAmount(),
                                                   BestBidPrice, MarketAssetId,
                                                   ORDER_BOOK_UPDATE_BUY);
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
        }

        void ExecuteMarketableOrder(
            Order &order) noexcept // assumption: the order IS marketable
        {
            std::cout << "Executing marketable order.\n\n";
            if (order.getSide() == OrderSide::BUY)
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

            BestAskPrice = std::min(BestAskPrice, order.getPrice());

            PriceLevel *curLevel = Ask.AddLimitOrder(toAdd);

            OrderBookUpdate *curOrderBookUpdate =
                OrderBookUpdatesPool.acquire();
            curOrderBookUpdate->FillUpdate(curLevel->GetTotalAmount(),
                                           curLevel->GetKey(), MarketAssetId,
                                           ORDER_BOOK_UPDATE_SELL);
            OutgoingBook.try_enqueue(curOrderBookUpdate);
        }

        void AddBuyLimitOrder(Order &order)
        {
            OrderNode *toAdd = OrderNodePool.acquire();
            toAdd->SetOrder(order);

            BestBidPrice = std::max(BestBidPrice, order.getPrice());

            PriceLevel *curLevel = Bid.AddLimitOrder(toAdd);

            OrderBookUpdate *curOrderBookUpdate =
                OrderBookUpdatesPool.acquire();
            curOrderBookUpdate->FillUpdate(curLevel->GetTotalAmount(),
                                           curLevel->GetKey(), MarketAssetId,
                                           ORDER_BOOK_UPDATE_BUY);
            OutgoingBook.try_enqueue(curOrderBookUpdate);
        }

        void AddLimitOrder(
            Order &order) noexcept // assumption: the order is not marketable
        {
            std::cout << "Adding to order book\n";
            const bool isBuy = order.getSide() == OrderSide::BUY;

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

                        executeOrder(curOrder);
                        std::cout << "finished execution\n";
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
} // namespace MarketExecution
