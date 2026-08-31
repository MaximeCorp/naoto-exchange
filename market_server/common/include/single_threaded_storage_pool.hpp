#pragma once

#include <concepts>
#include <iostream>
#include <vector>

namespace naoto
{
    template <typename T>
    class SingleThreadedStoragePool
    {
    private:
        std::vector<T> Storage;
        const size_t Capacity;
        size_t FreeSize;
        std::vector<T *> FreeQueue;

    public:
        SingleThreadedStoragePool(size_t capacity)
            : Capacity(capacity)
            , FreeSize(capacity)
        {
            if (capacity == 0)
            {
                throw std::invalid_argument(
                    "Pool size must be greater than zero.");
            }

            std::cout << "Initializing storage pool with capacity: "
                      << Capacity << " objects.\n";

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
            T *res = nullptr;

            if (FreeSize > 0) [[likely]]
            {
                res = FreeQueue[--FreeSize];
            }

            return res;
        }

        [[nodiscard]] bool release(T *toRelease) noexcept
        {
            if (toRelease && FreeSize < Capacity) [[likely]]
            {
                FreeQueue[FreeSize++] = toRelease;
                return true;
            }

            return false;
        }
    };
} // namespace naoto
