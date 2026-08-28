#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace naoto
{
    template <typename T, size_t BatchSize>
    struct ObjectBatch
    {
        alignas(64) std::array<T, BatchSize> Data;
        alignas(64) size_t Size = 0;
        alignas(64) uint32_t Fd = 0;
        alignas(64) uint8_t Auth = 0;

        ObjectBatch(void) = default;

        ObjectBatch(ObjectBatch &&) = default;
        ObjectBatch &operator=(ObjectBatch &&) = default;

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

        [[nodiscard]] T &operator[](size_t idx) noexcept
        {
            return Data[idx];
        }

        [[nodiscard]] const T &operator[](size_t idx) const noexcept
        {
            return Data[idx];
        }
    };
} // namespace naoto
