#pragma once

#include <array>
#include <iostream>
#include <readerwritercircularbuffer.h>
#include <spsc_queue.hpp>
#include <stdexcept>

namespace naoto
{
    template <typename T, size_t Size>
    class StoragePool
    {
        using FreeQueue = SpscQueue<T *, Size>;

    private:
        std::array<T, Size> Storage;
        FreeQueue Free;

        alignas(64) size_t LocalHead;
        alignas(64) size_t LocalTail;

        alignas(64) std::array<T *, Size> LocalReuseBuffer;
        size_t LocalReuseSize;

    public:
        StoragePool(void)
            : LocalHead(0)
            , LocalTail(0)
            , LocalReuseSize(0)
        {
            if (Size == 0)
            {
                throw std::invalid_argument(
                    "Pool size must be greater than zero.");
            }

            std::cout << "Initializing storage pool with capacity: " << Size
                      << " objects.\n";

            for (size_t i = 0; i < Size; ++i)
            {
                T *ptr = &Storage[i];

                if (!Free.TryPush(ptr, LocalTail))
                {
                    throw std::runtime_error(
                        "Failed to populate initial free list.");
                }

                LocalReuseBuffer[i] = nullptr;
            }
            std::cout << "Pool ready. All " << Size
                      << " objects are available.\n";
        }

        [[nodiscard]] size_t GetSize() const noexcept
        {
            return Size;
        }

        // Producer methods

        [[nodiscard]] T *Acquire() noexcept
        {
            if (LocalReuseSize)
            {
                return LocalReuseBuffer[--LocalReuseSize];
            }

            T *res;

            bool found = Free.TryPop(res, LocalTail);

            return found ? res : nullptr;
        }

        [[nodiscard]] bool LocalRelease(T *element) noexcept
        {
            bool released = LocalReuseSize < Size;

            if (released)
            {
                LocalReuseBuffer[LocalReuseSize++] = element;
            }

            return released;
        }

        [[nodiscard]] bool GetAvailable() noexcept
        {
            return LocalReuseSize > 0 || Free.GetSize() != 0;
        }

        // Consumer methods

        [[nodiscard]] bool Release(T *element) noexcept
        {
            return element && Free.TryPush(element, LocalHead);
        }

        void ReleaseCritical(T *element) noexcept
        {
            if (!element || !Free.TryPush(element, LocalHead)) [[unlikely]]
            {
                std::fprintf(stderr,
                             "CRITICAL: Mempool corruption. Failed "
                             "to release batch %p\n",
                             element);
                std::terminate();
            }
        }
    };
} // namespace naoto
