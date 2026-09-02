#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace naoto::order_gateway
{
    template <size_t MaxPositions>
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
        // BUG FIX: previously only initialized Auth/SessionId. ClientId,
        // AssetId, Confirmed, and Attempt were left at indeterminate
        // values (this is a class with a user-provided constructor, so
        // members missing from the init list are NOT zero-initialized).
        // ClientStates builds States1/2/3 as
        // std::vector<ClientState<MaxPositions>>(maxClients), which
        // default-constructs every element via exactly this
        // constructor - so every "fresh" client (before any
        // SetClientState()) had garbage Confirmed/Attempt instead of
        // the zero baseline the whole delta-accumulation design
        // assumes. Confirmed by tests/market_server/unit/test_client_states_triple_buffer.cpp's
        // FreshStateStartsAtZero, which read back actual heap garbage
        // before this fix (see tests/market_server/README.md for a worse manifestation:
        // combined with the ClientAccountSnapshot alignment issue in the
        // same file, this produced genuinely wrong Confirmed/Attempt
        // values - not just theoretical UB - once built with -O3).

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
