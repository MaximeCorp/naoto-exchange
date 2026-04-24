#include <Order.hpp>
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
        Order *Head; // Doubly linked list of orders
        Order *Tail;

    public:
        PriceLevel(std::int64_t price)
            : Price(price)
            , Generation(0)
            , Size(0)
            , TotalAmount(0)
            , Head(nullptr)
            , Tail(nullptr)
        {}

        [[nodiscard]] bool addOrder(Order *order) noexcept
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

        [[nodiscard]] Order *peekOrder(void) noexcept
        {
            return Head;
        }

        [[nodiscard]] Order *
        popOrder(void) noexcept // The programmer is in charge to free/release
                                // the orders
        {
            Order *res = Head;

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
    };
} // namespace MarketExecution
