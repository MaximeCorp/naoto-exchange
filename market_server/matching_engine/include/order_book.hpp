#pragma once

#include <flat_hash_map.hpp>
#include <iostream>
#include <order_node.hpp>
#include <price_level.hpp>
#include <skip_list.hpp>
#include <single_threaded_storage_pool.hpp>

namespace naoto::matching_engine
{
    template <size_t FHMSize, size_t SkipListMaxLevel,
              typename Compare = std::less<std::int64_t>>
    class OrderBook
    {
    private:
        FlatHashMap<int64_t, PriceLevel *, FHMSize> FastMap;
        SkipList<int64_t, PriceLevel *, SkipListMaxLevel, Compare>
            BestPricesMap;
        SingleThreadedStoragePool<OrderNode> &OrderNodePool;
        SingleThreadedStoragePool<PriceLevel> &PriceLevelPool;

    public:
        OrderBook(const size_t skipListNodesPoolSize,
                  SingleThreadedStoragePool<OrderNode> &orderNodePool,
                  SingleThreadedStoragePool<PriceLevel> &priceLevelPool)
            : BestPricesMap(skipListNodesPoolSize)
            , OrderNodePool(orderNodePool)
            , PriceLevelPool(priceLevelPool)
        {}

        [[nodiscard]] PriceLevel *GetBestLevel(void) noexcept
        {
            return BestPricesMap.GetHead();
        }

        [[nodiscard]] OrderNode *GetBestOffer(void) noexcept
        {
            PriceLevel *bestPrice = BestPricesMap.GetHead();

            if (!bestPrice) [[unlikely]]
            {
                return nullptr;
            }

            return bestPrice->PeekOrder();
        }

        [[nodiscard]] PriceLevel *AddLimitOrder(OrderNode *order) noexcept
        {
            int64_t key = order->GetPrice();

            PriceLevel *curPrice;
            bool found = FastMap.GetVal(key, curPrice);

            if (!found) [[unlikely]]
            {
                curPrice = PriceLevelPool.acquire();

                if (!curPrice) [[unlikely]]
                {
                    std::cerr << "couldn't acquire price level\n";
                    std::terminate();
                }

                curPrice->ClearPriceLevel(OrderNodePool);
                curPrice->SetPrice(key);

                FastMap.AddNode(key, curPrice);
                BestPricesMap.AddNode(key, curPrice);
            }

            curPrice->AddOrder(order);

            return curPrice;
        }

        PriceLevel *DeleteOrder(OrderNode *order) noexcept
        {
            const int64_t key = order->GetPrice();
            PriceLevel *curPrice;

            bool found = FastMap.GetVal(key, curPrice);

            if (!found) [[unlikely]]
            {
                std::cerr << "tried deleting node with no price level\n";
                return nullptr;
            }

            if (curPrice->DeleteOrder(order)) [[unlikely]]
            {
                FastMap.DeleteNode(key);
                BestPricesMap.DeleteNode(key);
                bool released = PriceLevelPool.release(curPrice);

                if (!released) [[unlikely]]
                {
                    std::cerr << "Couldn't release to price level mempool when "
                                 "trying to "
                                 "delete an order.\n\n";
                }

                return nullptr;
            }

            bool released = OrderNodePool.release(order);

            if (!released) [[unlikely]]
            {
                std::cerr << "Couldn't release to order mempool when trying to "
                             "delete an order.\n\n";
            }

            return curPrice;
        }
    };
} // namespace naoto::matching_engine
