#pragma once

#include <Order.hpp>
#include <boost/lockfree/queue.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Gateways
{
    template <class T>
    class StoragePool
    {
    private:
        std::vector<T> OrderStorage;

        const size_t Capacity;
        boost::lockfree::queue<T *> FreeOrders;

    public:
        StoragePool(size_t poolSize)
            : Capacity(poolSize)
            , FreeOrders(poolSize)
        {
            if (poolSize == 0)
            {
                throw std::invalid_argument(
                    "Pool size must be greater than zero.");
            }

            std::cout << "Initializing Order Pool with capacity: " << Capacity
                      << " orders.\n";

            OrderStorage.resize(Capacity);

            for (size_t i = 0; i < Capacity; ++i)
            {
                T *ptr = &OrderStorage[i];

                if (!FreeOrders.push(ptr))
                {
                    throw std::runtime_error(
                        "Failed to populate initial free list.");
                }
            }
            std::cout << "Pool ready. All " << Capacity
                      << " objects are available.\n";
        }

        T *acquire()
        {
            T *res = nullptr;

            if (FreeOrders.pop(res))
            {
                return res;
            }

            return nullptr;
        }

        void release(T *element)
        {
            if (!element)
                return;

            if (!FreeOrders.push(element))
            {
                std::cerr << "CRITICAL ERROR: Free list push failed during "
                             "release.\n";
            }
        }

        size_t getCapacity() const
        {
            return Capacity;
        }
        bool getAvailable() const
        {
            return !FreeOrders.empty();
        }
    };
} // namespace Gateways
