#pragma once

#include <order.hpp>
#include <reader_writer_circular_buffer.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Gateways
{
    template <typename T>
    class StoragePool
    {
        using FreeQueue = moodycamel::BlockingReaderWriterCircularBuffer<T *>;

    private:
        std::vector<T> OrderStorage;

        const size_t Capacity;
        FreeQueue Free;
        std::vector<T *> LocalReuseBuffer;
        size_t LocalReuseSize;

    public:
        StoragePool(size_t poolSize)
            : Capacity(poolSize)
            , Free(poolSize)
            , LocalReuseSize(0)
        {
            if (poolSize == 0)
            {
                throw std::invalid_argument(
                    "Pool size must be greater than zero.");
            }

            std::cout << "Initializing Order Pool with capacity: " << Capacity
                      << " orders.\n";

            OrderStorage.resize(Capacity);

            LocalReuseBuffer.resize(Capacity);

            for (size_t i = 0; i < Capacity; ++i)
            {
                T *ptr = &OrderStorage[i];

                if (!Free.try_enqueue(ptr))
                {
                    throw std::runtime_error(
                        "Failed to populate initial free list.");
                }
                LocalReuseBuffer[i] = nullptr;
            }
            std::cout << "Pool ready. All " << Capacity
                      << " objects are available.\n";
        }

        [[nodiscard]] size_t getCapacity() const noexcept
        {
            return Capacity;
        }

        // Producer methods

        [[nodiscard]] T *acquire() noexcept
        {
            if (LocalReuseSize)
            {
                return LocalReuseBuffer[--LocalReuseSize];
            }

            T *res = nullptr;

            Free.try_dequeue(res);
            return res;
        }

        [[nodiscard]] bool localRelease(T *element) noexcept
        {
            bool released = LocalReuseSize < Capacity;

            if (released)
            {
                LocalReuseBuffer[LocalReuseSize++] = element;
            }

            return released;
        }

        [[nodiscard]] bool getAvailable() const noexcept
        {
            return LocalReuseSize > 0 || Free.peek() != nullptr;
        }

        // Consumer methods

        [[nodiscard]] bool release(T *element) noexcept
        {
            return element && Free.try_enqueue(element);
        }

        void releaseCritical(T *element) noexcept
        {
            if (!element || !Free.try_enqueue(element)) [[unlikely]]
            {
                std::fprintf(stderr,
                             "CRITICAL: Mempool corruption. Failed "
                             "to release batch %p\n",
                             element);
                std::terminate();
            }
        }
    };
} // namespace Gateways
