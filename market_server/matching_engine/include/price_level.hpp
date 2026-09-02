#pragma once

#include <cstdint>
#include <iostream>
#include <order_node.hpp>
#include <single_threaded_storage_pool.hpp>

namespace naoto::matching_engine
{
    class PriceLevel
    {
    private:
        int64_t Price;
        size_t Size;
        uint64_t TotalAmount;
        OrderNode *Head; // Doubly linked list of orders
        OrderNode *Tail;

    public:
        PriceLevel(void)
            : Price(0)
            , Size(0)
            , TotalAmount(0)
            , Head(nullptr)
            , Tail(nullptr)
        {}

        PriceLevel(std::int64_t price)
            : Price(price)
            , Size(0)
            , TotalAmount(0)
            , Head(nullptr)
            , Tail(nullptr)
        {}

        void AddOrder(OrderNode *order) noexcept
        {
            if (!Head) [[unlikely]]
            {
                Head = order;
                Tail = order; // Increment generation because size is 0
                order->SetPrev(nullptr);
                order->SetNext(nullptr);
            }
            else
            {
                Tail->SetNext(order);
                order->SetPrev(Tail);
                order->SetNext(nullptr);
                Tail = order;
            }

            ++Size;
            TotalAmount += order->GetAmount();
        }

        [[nodiscard]] bool DeleteOrder(
            OrderNode *order) noexcept // Assumes order is actually contained
        {
            --Size;
            TotalAmount -= order->GetAmount();

            if (order->GetPrev())
            {
                order->GetPrev()->SetNext(order->GetNext());
            }
            else
            {
                SetHead(order->GetNext());
            }

            if (order->GetNext())
            {
                order->GetNext()->SetPrev(order->GetPrev());
            }
            else
            {
                SetTail(order->GetPrev());
            }

            return Size == 0;
        }

        [[nodiscard]] OrderNode *PeekOrder(void) const noexcept
        {
            return Head;
        }

        [[nodiscard]] OrderNode *
        PopOrder(void) noexcept // The programmer is in charge to free/release
                                // the orders
        {
            if (!Head) [[unlikely]]
            {
                return nullptr;
            }

            OrderNode *res = Head;

            Head = Head ? Head->GetNext() : nullptr;

            if (Head) [[likely]]
            {
                Head->SetPrev(nullptr);
            }
            else
            {
                Tail = nullptr;
            }

            --Size;
            TotalAmount -= res->GetAmount();

            // If size is zero, push this price level to "empty price
            // levels" queue

            return res;
        }

        void ClearPriceLevel(
            SingleThreadedStoragePool<OrderNode> &orderNodePool)
        {
            Size = 0;
            TotalAmount = 0;

            OrderNode *curNode = Head;

            // BUG FIX: this used to advance `curNode` to the next node
            // *before* releasing `toRelease`, then `break` as soon as
            // `curNode` became null (i.e. right after processing the
            // second-to-last node) - so the last node in the list was
            // never released back to the pool. Confirmed by
            // tests/market_server/unit/test_price_level.cpp: with a
            // 3-node level and a 3-capacity pool, only 2 nodes came
            // back after ClearPriceLevel(). Fixed by releasing every
            // node the loop visits, including the last one.
            while (curNode)
            {
                OrderNode *toRelease = curNode;
                curNode = curNode->GetNext();

                bool released = orderNodePool.release(toRelease);

                if (!released) [[unlikely]]
                {
                    std::cerr
                        << "Failed to release while clearing price level\n";
                    std::terminate();
                }
            }

            Head = nullptr;
            Tail = nullptr;
        }

        void SetPrice(std::int64_t price) noexcept
        {
            Price = price;
        }

        void SetHead(OrderNode *order) noexcept
        {
            Head = order;
        }

        void SetTail(OrderNode *order) noexcept
        {
            Tail = order;
        }

        void IncTotalAmount(int64_t amountDelta) noexcept
        {
            TotalAmount += amountDelta;
        }

        [[nodiscard]] int64_t GetKey(void) const noexcept
        {
            return Price;
        }

        [[nodiscard]] uint64_t GetTotalAmount(void) const noexcept
        {
            return TotalAmount;
        }

        void log(void) const noexcept
        {
            std::cout << "PriceLevel[price=" << Price << ", size=" << Size
                      << ", totalAmount=" << TotalAmount
                      << ", head=" << static_cast<void *>(Head)
                      << ", tail=" << static_cast<void *>(Tail) << "]\n";
        }
    };
} // namespace naoto::matching_engine
