#include <OrderNode.hpp>
#include <cstdint>

namespace MarketExecution
{
    class PriceLevel
    {
    private:
        std::int64_t Price;
        size_t Generation;
        size_t Size;
        std::int64_t TotalAmount;
        OrderNode *Head; // Doubly linked list of orders
        OrderNode *Tail;

    public:
        PriceLevel(void)
            : Price(10)
            , Generation(0)
            , Size(0)
            , TotalAmount(0)
            , Head(nullptr)
            , Tail(nullptr)
        {}
        PriceLevel(std::int64_t price)
            : Price(price)
            , Generation(0)
            , Size(0)
            , TotalAmount(0)
            , Head(nullptr)
            , Tail(nullptr)
        {}

        [[nodiscard]] bool addOrder(OrderNode *order) noexcept
        {
            if (!Head) [[unlikely]]
            {
                Head = order;
                Tail = order;
                ++Generation; // Increment generation because size is 0
            }
            else
            {
                Tail->next = order;
                order->prev = Tail;
                Tail = order;
            }

            ++Size;
            TotalAmount += order->getAmount();

            return true;
        }

        [[nodiscard]] OrderNode *peekOrder(void) noexcept
        {
            return Head;
        }

        [[nodiscard]] OrderNode *
        popOrder(void) noexcept // The programmer is in charge to free/release
                                // the orders
        {
            OrderNode *res = Head;

            Head = Head ? Head->next : nullptr;

            if (Head) [[likely]]
            {
                Head->prev = nullptr;
            }
            else
            {
                Tail = nullptr;
            }

            --Size;
            TotalAmount -= res->getAmount();

            // If size is zero, push this price level to "empty price levels"
            // queue

            return res;
        }

        void SetPrice(std::int64_t price) noexcept
        {
            Price = price;
        }

        [[nodiscard]] std::int64_t GetKey(void) const noexcept
        {
            return Price;
        }
    };
} // namespace MarketExecution
