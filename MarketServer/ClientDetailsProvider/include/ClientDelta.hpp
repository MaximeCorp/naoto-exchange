#pragma once

#include <cstddef>
#include <cstdint>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions>
    struct ClientDelta
    {
        // std::array<uint16_t, MaxPositions> AssetId; // Might be needed for
        // when switching assets
        std::array<int64_t, MaxPositions> Confirmation;
        std::array<int64_t, MaxPositions> Attempt;
    };
} // namespace ClientDetailsProvider