#pragma once

#include <FlatHashMap.hpp>
#include <PriceLevel.hpp>
#include <SkipList.hpp>
#include <UnsafeStoragePool.hpp>

namespace MarketExecution
{
    template <size_t FHMSize, size_t SkipListMaxLevel,
              typename Compare = std::less<std::int64_t>>
    class OrderBook
    {
    private:
        FlatHashMap<int64_t, PriceLevel *, FHMSize> FastMap;
        SkipList<int64_t, PriceLevel *, SkipListMaxLevel, Compare>
            BestPricesMap;
        UnsafeStoragePool<OrderNode> &OrderNodePool;
        UnsafeStoragePool<PriceLevel> &PriceLevelPool;

    public:
        OrderBook(const size_t skipListNodesPoolSize,
                  UnsafeStoragePool<OrderNode> &orderNodePool,
                  UnsafeStoragePool<PriceLevel> &priceLevelPool)
            : BestPricesMap(skipListNodesPoolSize)
            , OrderNodePool(orderNodePool)
            , PriceLevelPool(priceLevelPool)
        {}

        [[nodiscard]] OrderNode *GetBestOffer(void) noexcept
        {
            PriceLevel *bestPrice = BestPricesMap.GetHead();

            if (!bestPrice) [[unlikely]]
            {
                return nullptr;
            }

            return bestPrice->PeekOrder();
        }

        void AddLimitOrder(OrderNode *order) noexcept
        {
            int64_t key = order->GetKey();

            PriceLevel *curPrice = FastMap.GetVal(key);

            if (!curPrice) [[unlikely]]
            {
                curPrice = PriceLevelPool.acquire();

                if (!curPrice) [[unlikely]]
                {
                    std::terminate();
                }

                curPrice->ClearPriceLevel(OrderNodePool);
                curPrice->SetPrice(key);

                FastMap.AddNode(key, curPrice);
                BestPricesMap.AddNode(key, curPrice);
            }

            curPrice->AddOrder(order);
        }

        void DeleteOrder(OrderNode *order) noexcept
        {
            const int64_t key = order->GetKey();
            PriceLevel *curPrice = FastMap.GetVal(key);

            if (!curPrice) [[unlikely]]
            {
                std::terminate();
            }

            if (curPrice->DeleteOrder(order)) [[unlikely]]
            {
                FastMap.DeleteNode(key);
                BestPricesMap.DeleteNode(key);
                bool released = PriceLevelPool.release(curPrice);

                if (!released) [[unlikely]]
                {
                    std::terminate();
                }
            }

            bool released = OrderNodePool.release(order);

            if (!released) [[unlikely]]
            {
                std::terminate();
            }
        }
    };
} // namespace MarketExecution
