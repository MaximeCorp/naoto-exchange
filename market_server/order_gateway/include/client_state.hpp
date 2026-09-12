#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <system_conf.hpp>

namespace naoto::order_gateway
{
    struct alignas(64) ClientState
    {
        uint32_t ClientId;
        std::array<uint16_t, MaxPositions> AssetId;
        std::array<int64_t, MaxPositions> Confirmed;
        std::array<int64_t, MaxPositions> Attempt;
        uint8_t Auth;
        uint32_t SessionId;

        ClientState(void)
            : ClientId(0)
            , AssetId{}
            , Confirmed{}
            , Attempt{}
            , Auth(0)
            , SessionId(0)
        {}

        [[nodiscard]] bool GetAssetIdx(const uint16_t assetId,
                                       size_t &idx) const noexcept
        {
            for (size_t i = 0; i < MaxPositions; ++i)
            {
                if (AssetId[i] == assetId)
                {
                    idx = i;
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] int64_t
        GetAssetConfirmed(const uint16_t assetId) const noexcept
        {
            int64_t confirmed = -1;

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                if (AssetId[i] == assetId)
                {
                    confirmed = Confirmed[i];
                    break;
                }
            }

            return confirmed;
        }

        [[nodiscard]] int64_t
        GetAssetAttempt(const uint16_t assetId) const noexcept
        {
            int64_t attempt = -1;

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                if (AssetId[i] == assetId)
                {
                    attempt = Attempt[i];
                    break;
                }
            }

            return attempt;
        }

        [[nodiscard]] uint16_t GetAssetIdAt(
            const size_t idx) const noexcept // Must be called with right
                                             // index or terminate
        {
            return AssetId[idx];
        }

        [[nodiscard]] int64_t GetConfirmedAt(
            const size_t idx) const noexcept // Must be called with right
                                             // index or terminate
        {
            return Confirmed[idx];
        }

        [[nodiscard]] int64_t GetAttemptAt(
            const size_t idx) const noexcept // Must be called with right
                                             // index or terminate
        {
            return Attempt[idx];
        }

        void log() const noexcept
        {
            std::cout << "ClientState { ClientId=" << ClientId
                      << ", Auth=" << static_cast<int>(Auth) << " }\n";
            std::cout << std::setw(10) << "AssetId" << std::setw(15)
                      << "Confirmed" << std::setw(15) << "Attempt" << '\n';

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                std::cout << std::setw(10) << AssetId[i] << std::setw(15)
                          << Confirmed[i] << std::setw(15) << Attempt[i]
                          << '\n';
            }
        }
    };
} // namespace naoto::order_gateway
