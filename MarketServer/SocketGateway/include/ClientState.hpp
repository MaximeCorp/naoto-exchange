#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Gateways
{
    template <size_t MaxPositions>
    class alignas(64) ClientState
    {
    private:
        uint32_t ClientId;
        std::array<uint16_t, MaxPositions> AssetId;
        std::array<int64_t, MaxPositions> Confirmed;
        std::array<int64_t, MaxPositions> Attempt;

    public:
        ClientState(void)
        {}

        ClientState(uint32_t clientId)
            : ClientId(clientId)
        {}

        void SetClientId(uint32_t clientId) noexcept
        {
            ClientId = clientId;
        }

        void SetConfirmed(int64_t confirmed) noexcept
        {
            Confirmed = confirmed;
        }

        void SetAttempt(int64_t attempt) noexcept
        {
            Attempt = attempt;
        }

        [[nodiscard]] uint32_t GetClientId() const noexcept
        {
            return ClientId;
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
    };
} // namespace Gateways
