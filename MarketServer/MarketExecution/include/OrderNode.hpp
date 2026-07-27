#pragma once
#include <Order.hpp>

namespace MarketExecution
{
    struct OrderNode
    {
        Order NodeOrder;
        OrderNode *Prev;
        OrderNode *Next;

        OrderNode() = default;

        OrderNode(const Order &order)
            : NodeOrder(order)
            , Prev(nullptr)
            , Next(nullptr)
        {}

        OrderNode(const uint32_t id, OrderType type, OrderSide side,
                  std::int64_t price, std::int32_t client_id,
                  std::uint32_t amount, std::int32_t asset,
                  std::int64_t timestamp)
            : NodeOrder(id, type, side, price, client_id, amount, asset,
                        timestamp)
            , Prev(nullptr)
            , Next(nullptr)
        {}

        void SetOrder(const Order &order) noexcept
        {
            NodeOrder = order;
        }

        void SetAmount(const uint32_t amount) noexcept
        {
            NodeOrder.setAmount(amount);
        }

        void SetPrev(OrderNode *prev) noexcept
        {
            Prev = prev;
        }

        void SetNext(OrderNode *next) noexcept
        {
            Next = next;
        }

        [[nodiscard]] OrderNode *GetPrev(void) noexcept
        {
            return Prev;
        }

        [[nodiscard]] OrderNode *GetNext(void) noexcept
        {
            return Next;
        }

        [[nodiscard]] uint32_t GetAmount(void) const noexcept
        {
            return NodeOrder.getAmount();
        }

        [[nodiscard]] uint32_t GetId(void) const noexcept
        {
            return NodeOrder.getId();
        }

        [[nodiscard]] uint32_t GetClientId(void) const noexcept
        {
            return NodeOrder.getClientId();
        }

        [[nodiscard]] int64_t GetPrice(void) const noexcept
        {
            return NodeOrder.getPrice();
        }

        void log(void) noexcept
        {
            NodeOrder.log();
        }
    };
}; // namespace MarketExecution
