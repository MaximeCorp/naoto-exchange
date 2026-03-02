#pragma once

#include <cstdint>

namespace Gateways
{
#pragma pack(push, 1)
    enum class UpdateType : std::int32_t
    {
        EXECUTED,
        CANCELLED
    };

    class MarketUpdates
    {
    private:
        std::uint32_t Fd;
        std::uint32_t ClientId;
        // Confirmed amount of money
        std::int64_t Confirmed;

        // Amount of money taking pending transactions into account
        std::int64_t Attempt;
        UpdateType Type;
    };
#pragma pack(pop)
} // namespace Gateways
