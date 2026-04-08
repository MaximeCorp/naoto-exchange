#pragma once

#include <Order.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace MarketExecution
{
    template <typename T>
    class StoragePool
    {
        using FreeQueue = moodycamel::BlockingReaderWriterCircularBuffer<T *>;

    private:
        std::vector<T> OrderStorage;

        const size_t Capacity;
        FreeQueue Free;

    public:
        StoragePool(size_t poolSize)
            : Capacity(poolSize)
            , Free(poolSize)
        {
            if (poolSize == 0)
            {
                throw std::invalid_argument(
                    "Pool size must be greater than zero.");
            }

            std::cout << "Initializing Order Pool with capacity: " << Capacity
                      << " orders.\n";

            OrderStorage.reserve(Capacity);

            for (size_t i = 0; i < Capacity; ++i)
            {
                T *ptr = &OrderStorage[i];

                if (!Free.try_enqueue(ptr))
                {
                    throw std::runtime_error(
                        "Failed to populate initial free list.");
                }
            }
            std::cout << "Pool ready. All " << Capacity
                      << " objects are available.\n";
        }

        [[nodiscard]] T *acquire() noexcept
        {
            T *res = nullptr;

            Free.try_dequeue(res);
            return res;
        }

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

        [[nodiscard]] size_t getCapacity() const noexcept
        {
            return Capacity;
        }
        [[nodiscard]] bool getAvailable() const noexcept
        {
            return Free.peek() != nullptr;
        }
    };
} // namespace MarketExecution
