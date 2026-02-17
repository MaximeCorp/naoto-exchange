#pragma once

#include <Order.hpp>
#include <array>
#include <cstdint>

namespace Gateways
{
    template <size_t BatchSize>
    class OrderBatch
    {
    public:
        alignas(64) std::array<Order, BatchSize> Data;

    private:
        alignas(64) size_t Size = 0;
        alignas(64) std::uint32_t Fd = 0;

    public:
        OrderBatch(void) = default;

        OrderBatch(OrderBatch &&) = default;
        OrderBatch &operator=(OrderBatch &&) = default;

        inline void setSize(size_t size) noexcept
        {
            Size = size;
        }

        inline void setFd(std::uint32_t fd) noexcept
        {
            Fd = fd;
        }

        [[nodiscard]] inline size_t getSize(void) noexcept
        {
            return Size;
        }

        [[nodiscard]] inline std::uint32_t getFd(void) noexcept
        {
            return Fd;
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
