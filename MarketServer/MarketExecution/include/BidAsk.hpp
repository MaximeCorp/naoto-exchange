#pragma once

#include <Asset.hpp>
#include <Order.hpp>
#include <OrderBatch.hpp>
#include <OrderBook.hpp>
#include <OrderNode.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstdint>
#include <thread>
#include <vector>

namespace MarketExecution
{
    template <size_t FHMSize, size_t SkipListMaxLevel, size_t BatchSize>
    class BidAsk
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        uint32_t MarketAssetId;
        int64_t MarketPrice;
        int64_t BestAskPrice;
        int64_t BestBidPrice;

        OrdersQueue &IncomingOrders;
        OrdersQueue
            &OutgoingMarketUpdates; // Might wanna create proper object for this
        StoragePool<OrderBatch<BatchSize>> &OrdersPool;
        UnsafeStoragePool<OrderNode> OrderNodePool;
        UnsafeStoragePool<PriceLevel> PriceLevelPool;

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

        void FillBuyOrder(Order &order) noexcept
        {
            while (order.getAmount() > 0 && IsMarketable(order))
            {
                OrderNode *bestOffer = Ask.GetBestOffer();

                BestAskPrice = bestOffer->GetKey();

                if (!bestOffer) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    return;
                }

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

                    order.setAmount(orderAmount);
                    bestOffer->SetAmount(offerAmount);

                    std::cout << "Orders after matching\n:";
                    order.log();
                    bestOffer->log();

                    MarketPrice =
                        tradedAmount > 0 ? bestOffer->GetKey() : MarketPrice;

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
                OrderNode *bestOffer = Bid.GetBestOffer();

                if (!bestOffer) [[unlikely]]
                {
                    // Handle to letting know the client that order couldn't be
                    // fully filled
                    return;
                }

                BestBidPrice = bestOffer->GetKey();

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

                    order.setAmount(orderAmount);
                    bestOffer->SetAmount(offerAmount);

                    std::cout << "Orders after matching\n:";
                    order.log();
                    bestOffer->log();

                    MarketPrice =
                        tradedAmount > 0 ? bestOffer->GetKey() : MarketPrice;

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

            BestBidPrice = std::min(BestAskPrice, order.getPrice());

            Ask.AddLimitOrder(toAdd);
        }

        void AddBuyLimitOrder(Order &order)
        {
            OrderNode *toAdd = OrderNodePool.acquire();
            toAdd->SetOrder(order);

            BestBidPrice = std::max(BestBidPrice, order.getPrice());

            Bid.AddLimitOrder(toAdd);
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
               OrdersQueue &incoming, OrdersQueue &outgoing,
               StoragePool<OrderBatch<BatchSize>> &pool,
               const size_t orderNodePoolSize,
               const size_t skipListNodesPoolSize)
            : MarketAssetId(assetId)
            , MarketPrice(initialPrice)
            , BestAskPrice(INT64_MAX)
            , BestBidPrice(INT64_MIN)
            , IncomingOrders(incoming)
            , OutgoingMarketUpdates(outgoing)
            , OrdersPool(pool)
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
                OrderBatch<BatchSize> *batch = nullptr;

                if (IncomingOrders.try_dequeue(batch)) [[likely]]
                {
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
                        std::terminate();
                    }
                }
            }
        }
    };
} // namespace MarketExecution
