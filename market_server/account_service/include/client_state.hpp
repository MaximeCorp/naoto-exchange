#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <openssl/sha.h>

namespace naoto::account_service
{
    template <size_t MaxPositions>
    struct alignas(64) ClientState
    {
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
            SHA256(in.data(), 32, out.data());
            return out;
        }

        ClientState(void)
            : ClientId(0)
            , Key{}
            , AssetId{}
            , Confirmed{}
            , Attempt{}
            , Authorized(0)
            , Connected(0)
        {}
        // BUG FIX: same class of issue as order_gateway's ClientState
        // (see that header's fix comment) - Key/AssetId/Confirmed/Attempt
        // were missing from the init list and left indeterminate.
        // ClientStates builds States1/2/3 via
        // std::vector<ClientState<MaxPositions>>(maxClients), which
        // default-constructs every element through exactly this
        // constructor, so every "fresh" client had garbage funds data
        // and a garbage Key (meaning CheckKey() could spuriously pass or
        // fail depending on what was on the heap). Confirmed by
        // tests/market_server/unit/test_client_state_account_service.cpp.
        // BUG FIX: these two constructors took confirmed/attempt but
        // never stored them anywhere. Now that SetConfirmed()/
        // SetAttempt() exist (fixed alongside this) and establish what
        // "set confirmed/attempt from a scalar" means for this class -
        // fill every position uniformly - these two do the same thing,
        // for consistency, instead of silently discarding the argument.
        // Confirmed by
        // tests/market_server/unit/test_client_state_account_service.cpp.
        ClientState(uint32_t clientId, int64_t confirmed)
            : ClientId(clientId)
            , Key{}
            , AssetId{}
            , Confirmed{}
            , Attempt{}
            , Authorized(0)
            , Connected(0)
        {
            Confirmed.fill(confirmed);
        }

        ClientState(uint32_t clientId, int64_t confirmed, int64_t attempt)
            : ClientId(clientId)
            , Key{}
            , AssetId{}
            , Confirmed{}
            , Attempt{}
            , Authorized(0)
            , Connected(0)
        {
            Confirmed.fill(confirmed);
            Attempt.fill(attempt);
        }

        [[nodiscard]] bool
        CheckKey(const std::array<uint8_t, 32> &key) const noexcept
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

        // BUG FIX: these used to do `Confirmed = confirmed;` /
        // `Attempt = attempt;` - assigning a scalar int64_t directly to
        // a std::array<int64_t, MaxPositions> member, which does not
        // compile (no such std::array::operator=). Since these methods
        // are never called anywhere in the codebase today, the compile
        // error was latent - templates only instantiate members that
        // are actually used. Confirmed by
        // tests/market_server/unit/test_client_state_account_service.cpp, which does
        // call them. Fixed to fill every position with the given value,
        // the closest sensible meaning for a scalar "set confirmed/
        // attempt" call - flag if a per-asset-index setter was actually
        // intended instead.
        void SetConfirmed(int64_t confirmed) noexcept
        {
            Confirmed.fill(confirmed);
        }

        void SetAttempt(int64_t attempt) noexcept
        {
            Attempt.fill(attempt);
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
} // namespace naoto::account_service
