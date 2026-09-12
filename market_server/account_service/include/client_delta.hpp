#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <system_conf.hpp>

namespace naoto::account_service
{
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