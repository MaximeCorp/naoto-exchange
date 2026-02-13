#pragma once

#include <Order.hpp>
#include <array>

namespace Gateways
{
    template <size_t BatchSize>
    class OrderBatch
    {
    private:
        std::array<Order, BatchSize> Data;
        alignas(64) size_t Size = 0;

    public:
        OrderBatch(void) = default;

        OrderBatch(OrderBatch &&) = default;
        OrderBatch &operator=(OrderBatch &&) = default;

        inline void setSize(size_t size) noexcept
        {
            Size = size;
        }

        [[nodiscard]] inline size_t getSize(void) noexcept
        {
            return Size;
        }

        [[nodiscard]] inline Order &operator[](size_t idx) noexcept
        {
            return Data[idx];
        }

        [[nodiscard]] inline const Order &operator[](size_t idx) const noexcept
        {
            return Data[idx];
        }
    };
} // namespace Gateways
