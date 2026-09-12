#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <system_conf.hpp>

namespace naoto::order_gateway
{
    struct LocalAttempts
    {
        std::array<int64_t, MaxPositions> Attempt;

        [[nodiscard]] int64_t &operator[](const uint16_t assetId) noexcept
        {
            return Attempt[assetId];
        }

        [[nodiscard]] const int64_t &
        operator[](const uint16_t assetId) const noexcept
        {
            return Attempt[assetId];
        }

        LocalAttempts(void)
        {
            Clear();
        }

        void Clear(void) noexcept
        {
            Attempt.fill(0);
        }
    };
} // namespace naoto::order_gateway