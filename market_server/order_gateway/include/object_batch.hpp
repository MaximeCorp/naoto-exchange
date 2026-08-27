#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Gateways
{
    // TODO : Make sure there won't be false sharing in the batches pool
    template <typename T, size_t BatchSize>
    struct ObjectBatch
    {
        std::array<T, BatchSize> Data;
        size_t Size;
        std::uint32_t Fd;
        std::uint8_t Auth;

        ObjectBatch(void) = default;
        ObjectBatch(ObjectBatch &&) = default;
        ObjectBatch &operator=(ObjectBatch &&) = default;

        [[nodiscard]] T &operator[](size_t idx) noexcept
        {
            return Data[idx];
        }

        [[nodiscard]] const T &operator[](size_t idx) const noexcept
        {
            return Data[idx];
        }
    };
} // namespace Gateways
