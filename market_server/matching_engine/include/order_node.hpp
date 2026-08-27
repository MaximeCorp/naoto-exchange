#pragma once
#include <order.hpp>

namespace naoto::matching_engine
{
    struct OrderNode
    {
        OrderNode *Prev;
        OrderNode *Next;
        Order NodeOrder;

        OrderNode() = default;

        OrderNode(const Order &order)
            : Prev(nullptr)
            , Next(nullptr)
            , NodeOrder(order)
        {}

        void SetOrder(const Order &order) noexcept
        {
            NodeOrder = order;
        }

        void SetAmount(const uint32_t amount) noexcept
        {
            NodeOrder.Amount = amount;
        }

        void SetPrev(OrderNode *prev) noexcept
        {
            Prev = prev;
        }

        void SetNext(OrderNode *next) noexcept
        {
            Next = next;
        }

        [[nodiscard]] OrderNode *GetPrev(void) const noexcept
        {
            return Prev;
        }

        [[nodiscard]] OrderNode *GetNext(void) const noexcept
        {
            return Next;
        }

        [[nodiscard]] uint32_t GetAmount(void) const noexcept
        {
            return NodeOrder.Amount;
        }

        [[nodiscard]] uint32_t GetId(void) const noexcept
        {
            return NodeOrder.OrderId;
        }

        [[nodiscard]] uint32_t GetClientId(void) const noexcept
        {
            return NodeOrder.ClientId;
        }

        [[nodiscard]] int64_t GetPrice(void) const noexcept
        {
            return NodeOrder.Price;
        }

        void log(void) noexcept
        {
            NodeOrder.log();
        }
    };
}; // namespace naoto::matching_engine
