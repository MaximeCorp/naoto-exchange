#pragma once

#include <Asset.hpp>
#include <FlatHashMap.hpp>
#include <Order.hpp>
#include <OrderBatch.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstdint>
#include <thread>
#include <vector>

namespace MarketExecution
{
    template <size_t BatchSize>
    class BidAsk
    {
        using OrdersQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;
        using OrderBookMap =
            ska::flat_hash_map<std::int64_t, std::vector<Order>>;
        using OrderHandlingFunc = void (BidAsk<BatchSize>::*)(Order &);

    private:
        std::uint32_t MarketAssetId;
        std::int64_t MarketPrice;

        std::int64_t BestBidPrice;
        std::int64_t BestAskPrice;

        OrdersQueue &IncomingOrders;
        OrdersQueue
            &OutgoingMarketUpdates; // Might wanna create proper object for this
        StoragePool<OrderBatch<BatchSize>> &OrdersPool;

        OrderBookMap Bid;
        OrderBookMap Ask;
        // std::map<std::int64_t, std::vector<Order>, std::greater<>> Bid;
        // std::map<std::int64_t, std::vector<Order>> Ask;

        [[nodiscard]] std::vector<Order> *GetBestOffers(Order &order) noexcept
        {
            OrderBookMap &orderBook =
                order.getSide() == OrderSide::BUY ? Ask : Bid;
            std::int64_t bestPrice =
                order.getSide() == OrderSide::BUY ? BestAskPrice : BestBidPrice;

            std::vector<Order> &offers = orderBook[bestPrice];

            return offers.empty() ? nullptr : &offers;
        }

        [[nodiscard]] bool IsMarketable(Order &order) const noexcept
        {
            bool is_market = (order.getType() == OrderType::MARKET);
            bool is_buy = (order.getSide() == OrderSide::BUY);

            std::int64_t price = order.getPrice();

            bool limit_marketable =
                is_buy ? (price >= BestAskPrice) : (price <= BestBidPrice);

            return is_market | limit_marketable;
        }

        void FillOffer(Order &order, std::vector<Order> *bestOffers) noexcept
        {
            size_t i = 0;

            while (order.getAmount() > 0 && IsMarketable(order)
                   && i < bestOffers->size())
            {
                Order &curOffer = (*bestOffers)[i];

                std::cout << "Matching the following orders:\n";
                order.log();
                curOffer.log();

                const std::uint32_t curOrderAmount = order.getAmount();
                const std::uint32_t curOfferAmount = curOffer.getAmount();

                const uint32_t tradedAmount = (curOrderAmount < curOfferAmount)
                    ? curOrderAmount
                    : curOfferAmount;

                const std::uint32_t orderAmount = curOrderAmount - tradedAmount;
                const std::uint32_t offerAmount = curOfferAmount - tradedAmount;

                order.setAmount(orderAmount);
                curOffer.setAmount(offerAmount);

                std::cout << "Orders after matching\n:";
                order.log();
                curOffer.log();

                MarketPrice =
                    tradedAmount > 0 ? curOffer.getPrice() : MarketPrice;

                std::cout << "New market price:\n" << MarketPrice << "\n";

                i += (offerAmount == 0);
            }

            bestOffers->erase(
                bestOffers->begin(),
                bestOffers->begin()
                    + i); // O(n), should use circular buffer later

            // If bestOffers empty, best ask/bid price should be updated (not
            // easily doable with current map data structure)
        }

        void ExecuteMarketableOrder(
            Order &order) noexcept // assumption: the order IS marketable
        {
            do
            {
                std::vector<Order> *offers = GetBestOffers(order);

                if (offers == nullptr) [[unlikely]]
                {
                    // nullptr might not mean that we have to stop the loop
                    break;
                }

                FillOffer(order, offers);
            } while (order.getAmount() && IsMarketable(order));
        }

        void AddLimitOrder(
            Order &order) noexcept // assumption: the order is not marketable
        {
            std::cout << "Adding to order book\n";
            const bool isBuy = order.getSide() == OrderSide::BUY;
            const std::int64_t price = order.getPrice();

            // Get the correct map and price level
            OrderBookMap &orderBook = isBuy ? Bid : Ask;
            std::vector<Order> &priceLevel = orderBook[price];

            // Add the order
            priceLevel.emplace_back(order);

            // Update best price
            std::int64_t &bestPrice = isBuy ? BestBidPrice : BestAskPrice;
            bestPrice =
                isBuy ? std::max(bestPrice, price) : std::min(bestPrice, price);
        }

        static constexpr OrderHandlingFunc OrderHandlers[] = {
            &BidAsk<BatchSize>::AddLimitOrder,
            &BidAsk<BatchSize>::ExecuteMarketableOrder
        };

        void executeOrder(Order &order) noexcept
        {
            bool marketable = IsMarketable(order);
            auto handler = OrderHandlers[marketable];
            (this->*handler)(order);
        }

    public:
        BidAsk(std::uint32_t assetId, std::int64_t initialPrice,
               OrdersQueue &incoming, OrdersQueue &outgoing,
               StoragePool<OrderBatch<BatchSize>> &pool)
            : MarketAssetId(assetId)
            , MarketPrice(initialPrice)
            , BestBidPrice(INT64_MIN)
            , BestAskPrice(INT64_MAX)
            , IncomingOrders(incoming)
            , OutgoingMarketUpdates(outgoing)
            , OrdersPool(pool)
            , Bid()
            , Ask()
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
                }
            }
        }

        [[nodiscard]] int getMarketOrdersSize();

        [[nodiscard]] OrderBookMap &getBid()
        {
            return Bid;
        }

        [[nodiscard]] OrderBookMap &getAsk()
        {
            return Ask;
        }

        [[nodiscard]] std::int64_t getMarketPrice();

        [[nodiscard]] std::uint32_t getMarketAssetId();
    };
} // namespace MarketExecution
