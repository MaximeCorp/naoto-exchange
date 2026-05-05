#pragma once
#include <Order.hpp>

namespace MarketExecution
{
    struct OrderNode
    {
        Order order;
        OrderNode *prev;
        OrderNode *next;

        OrderNode() = default;

        OrderNode(const char *key, OrderType type, OrderSide side,
                  std::int64_t price, std::int32_t client_id,
                  std::uint32_t amount, std::int32_t asset,
                  std::int64_t timestamp)
            : order(key, type, side, price, client_id, amount, asset, timestamp)
            , prev(nullptr)
            , next(nullptr)
        {}

        std::uint32_t getAmount(void) const noexcept
        {
            return order.getAmount();
        }

        std::int64_t GetKey(void) const noexcept
        {
            return order.getPrice();
        }
    };
}; // namespace MarketExecution
