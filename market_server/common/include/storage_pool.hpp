#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <spsc_queue.hpp>
#include <stdexcept>

namespace naoto
{
    // Free list backing a StoragePool of Size objects of type T.
    template <typename T, size_t Size>
    using StoragePoolFreeQueue = SpscQueue<T *, Size>;

    template <typename T, size_t Size>
    class StoragePool
    {
    private:
        std::array<T, Size> Storage;
        StoragePoolFreeQueue<T, Size> Free;

        alignas(64) SpscQueueConsumer<T *, Size> Consumer;
        alignas(64) SpscQueueProducer<T *, Size> Producer;

        alignas(64) std::array<T *, Size> LocalReuseBuffer;
        size_t LocalReuseSize;

    public:
        StoragePool(void)
            : Consumer(&Free)
            , Producer(&Free)
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

                if (!Producer.TryPush(ptr))
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

            bool found = Consumer.TryPop(res);

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
            return element && Producer.TryPush(element);
        }

        void ReleaseCritical(T *element) noexcept
        {
            if (!element || !Producer.TryPush(element)) [[unlikely]]
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
