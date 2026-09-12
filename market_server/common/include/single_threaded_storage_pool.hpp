#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <iostream>
#include <vector>

namespace naoto
{
    template <typename T, size_t Size>
    class SingleThreadedStoragePool
    {
    private:
        std::array<T, Size> Storage;
        std::array<T *, Size> FreeQueue;
        size_t FreeSize;

    public:
        SingleThreadedStoragePool(void)
            : FreeSize(Size)
        {
            std::cout << "Initializing storage pool with capacity: " << Size
                      << " objects.\n";

            for (size_t i = 0; i < Size; ++i)
            {
                T *curElement = &Storage[i];
                FreeQueue[i] = curElement;
            }
        }

        [[nodiscard]] T *Acquire() noexcept
        {
            T *res = nullptr;

            if (FreeSize > 0) [[likely]]
            {
                res = FreeQueue[--FreeSize];
            }

            return res;
        }

        [[nodiscard]] bool Release(T *toRelease) noexcept
        {
            if (toRelease && FreeSize < Size) [[likely]]
            {
                FreeQueue[FreeSize++] = toRelease;
                return true;
            }

            return false;
        }
    };
} // namespace naoto
