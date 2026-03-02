#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Gateways
{
#pragma pack(push, 1)
    template <size_t BatchSize>
    class ClientStatesUpdates
    {
    public:
        alignas(64) std::array<std::uint32_t, BatchSize> Fd;
        alignas(64) std::array<std::uint32_t, BatchSize> Id;
        alignas(64) std::array<std::int64_t, BatchSize> Confirmed;

    private:
        size_t Size = 0;

    public:
        ClientStatesUpdates(void) = default;

        inline void setSize(const size_t size) noexcept
        {
            Size = size;
        }

        [[nodiscard]] inline size_t getSize(void) noexcept
        {
            return Size;
        }
    };
#pragma pack(pop)
} // namespace Gateways
