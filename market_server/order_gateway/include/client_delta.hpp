#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <system_conf.hpp>

namespace naoto::order_gateway
{
    struct ClientDelta
    {
        // std::array<uint16_t, MaxPositions> AssetId; // Might be needed for
        // when switching assets
        std::array<int64_t, MaxPositions> Confirmed;
        std::array<int64_t, MaxPositions> Attempt;
    };
} // namespace naoto::order_gateway
