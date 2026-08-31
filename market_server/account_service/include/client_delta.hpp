#pragma once

#include <cstddef>
#include <cstdint>

namespace naoto::account_service
{
    template <size_t MaxPositions>
    struct ClientDelta
    {
        // std::array<uint16_t, MaxPositions> AssetId; // Might be needed for
        // when switching assets
        std::array<int64_t, MaxPositions> Confirmed;
        std::array<int64_t, MaxPositions> Attempt;

        ClientDelta(void)
        {
            Confirmed.fill(0);
            Attempt.fill(0);
        }
    };
} // namespace naoto::account_service