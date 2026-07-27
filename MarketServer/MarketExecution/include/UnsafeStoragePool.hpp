#pragma once

#include <OrderNode.hpp>
#include <concepts>
#include <iostream>
#include <vector>

namespace MarketExecution
{
    template <typename T>
    class UnsafeStoragePool
    {
    private:
        std::vector<T> Storage;
        const size_t Capacity;
        size_t FreeSize;
        std::vector<T *> FreeQueue;

    public:
        UnsafeStoragePool(size_t capacity)
            : Capacity(capacity)
            , FreeSize(capacity)
        {
            if (capacity == 0)
            {
                throw std::invalid_argument(
                    "Pool size must be greater than zero.");
            }

            std::cout << "Initializing Order Pool with capacity: " << Capacity
                      << " orders.\n";

            Storage.resize(Capacity);
            FreeQueue.resize(Capacity);

            for (size_t i = 0; i < Capacity; ++i)
            {
                T *curElement = &Storage[i];
                FreeQueue[i] = curElement;
            }
        }

        [[nodiscard]] T *acquire() noexcept
        {
            if (std::is_same_v<T, OrderNode>)
            {
                std::cout << "acquiring from unsafe storage pool\n";
            }
            T *res = nullptr;

            if (FreeSize > 0) [[likely]]
            {
                res = FreeQueue[--FreeSize];
            }

            return res;
        }

        [[nodiscard]] bool release(T *toRelease) noexcept
        {
            if (std::is_same_v<T, OrderNode>)
            {
                std::cout << "releasing from unsafe storage pool\n";
            }

            if (toRelease && FreeSize < Capacity) [[likely]]
            {
                FreeQueue[FreeSize++] = toRelease;
                return true;
            }

            return false;
        }
    };
} // namespace MarketExecution
