#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions>
    struct alignas(64) ClientState
    {
    private:
        uint32_t ClientId;
        std::array<uint8_t, 32> Key;
        std::array<uint16_t, MaxPositions> AssetId;
        std::array<int64_t, MaxPositions> Confirmed;
        std::array<int64_t, MaxPositions> Attempt;
        int16_t Authorized; // Id of gateway that's assigned to client, -1 if no
                            // gateway assigned
        int16_t Connected; // Id of gateway the client is currently connected
                           // to, -1 if not connected

        [[nodiscard]] static std::array<uint8_t, 32>
        sha256(const std::array<uint8_t, 32> &in) noexcept
        {
            std::array<uint8_t, 32> out;
            SHA256(in.data(), len, out.data());
            return out;
        }

    public:
        ClientState(void)
            : ClientId(0)
            , Confirmed(0)
            , Attempt(0)
            , Authorized(0)
            , Connected(0)
        {}

        ClientState(uint32_t clientId, int64_t confirmed)
            : ClientId(clientId)
            , Confirmed(confirmed)
            , Attempt(0)
            , Authorized(0)
            , Connected(0)
        {}

        ClientState(uint32_t clientId, int64_t confirmed, int64_t attempt)
            : ClientId(clientId)
            , Confirmed(confirmed)
            , Attempt(attempt)
            , Authorized(0)
            , Connected(0)
        {}

        [[nodiscard]] bool CheckKey(std::array<uint8_t, 32> &key) const noexcept
        {
            std::array<uint8_t, 32> keyHash = sha256(key);

            for (size_t i = 0; i < 32; ++i)
            {
                if (keyHash[i] != Key[i])
                {
                    return false;
                }
            }

            return true;
        }

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

        void SetAuthorized(int16_t authorized) noexcept
        {
            Authorized = authorized;
        }

        void SetConnected(int16_t connected) noexcept
        {
            Connected = connected;
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

        [[nodiscard]] int16_t GetAuthorized() const noexcept
        {
            return Authorized;
        }

        [[nodiscard]] int16_t GetConnected() const noexcept
        {
            return Connected;
        }
    };
} // namespace ClientDetailsProvider
