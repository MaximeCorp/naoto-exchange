#pragma once

#include <ClientDelta.hpp>
#include <ClientState.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <atomic>
#include <vector>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions>
    class ClientStates // Data coherence not guaranteed, pls update the right
                       // buffers yourself when using flush
    {
    private:
        alignas(64) std::vector<ClientState<MaxPositions>> States1;
        alignas(64) std::vector<ClientState<MaxPositions>> States2;
        alignas(64) std::vector<ClientState<MaxPositions>> States3;

        alignas(
            64) std::vector<std::array<ClientDelta<MaxPositions>, 3>> Deltas;

        alignas(64) std::vector<std::atomic<uint8_t>> Complete;

    public:
        ClientStates(size_t maxClients)
            : States1(maxClients)
            , States2(maxClients)
            , States3(maxClients)
            , Deltas(maxClients)
            , Complete(maxClients, 0)
        {}

        // Consumer methods
        [[nodiscard]] uint8_t
        GetComplete(const uint32_t clientId) const noexcept
        {
            return Complete[clientId].load(std::memory_order_acquire);
        }

        [[nodiscard]] const ClientState<MaxPositions>
        GetClientState(const uint32_t clientId) const noexcept
        {
            uint8_t complete =
                Complete[clientId].load(std::memory_order_acquire);
            return complete == 0 ? States1[clientId]
                : complete == 1  ? States2[clientId]
                                 : States3[clientId];
        }

        // Producer methods
        void
        SetClientState(const ClientState<MaxPositions> &clientState) noexcept
        {
            const uint32_t clientId = clientState.ClientId;

            uint8_t complete =
                Complete[clientId].load(std::memory_order_relaxed);

            ClientState<MaxPositions> &toChange = complete == 0
                ? States2[clientId]
                : (complete == 1 ? States3[clientId] : States1[clientId]);
            toChange = clientState;

            // Should update deltas if this function is ever used
        }

        void SetClientAssets(const uint32_t clientId, const int64_t confirmed,
                             const int64_t attempt, uint16_t assetId) noexcept
        {
            uint8_t complete =
                Complete[clientId].load(std::memory_order_relaxed);

            ClientState<MaxPositions> &toChange = complete == 0
                ? States2[clientId]
                : (complete == 1 ? States3[clientId] : States1[clientId]);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                if (toChange.AssetId[i] == assetId)
                {
                    Deltas[clientId][complete].Confirmed[i] += confirmed;
                    Deltas[clientId][complete].Attempt[i] += attempt;
                }
            }
        }

        void FlushTripleBuffer(const uint32_t clientId) noexcept
        {
            uint8_t complete =
                Complete[clientId].load(std::memory_order_relaxed);

            std::array<ClientDelta<MaxPositions>, 3> &curDelta =
                Deltas[clientId];

            ClientState<MaxPositions> *state = complete == 0
                ? &States2[clientId]
                : (complete == 1 ? &States3[clientId] : &States1[clientId]);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                state->Confirmed[i] += curDelta[complete].Confirmed[i];
                state->Attempt[i] += curDelta[complete].Attempt[i];
            }

            complete = complete == 2 ? 0 : complete + 1;

            Complete[clientId].store(complete, std::memory_order_release);

            // Assumption: the client details will always contain MaxPositions
            // assets (even if some aren't used)

            state = complete == 0
                ? &States2[clientId]
                : (complete == 1 ? &States3[clientId] : &States1[clientId]);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                curDelta[complete].Confirmed[i] = 0;
                curDelta[complete].Attempt[i] = 0;
                state->Confirmed[i] += curDelta[0].Confirmed[i];
                state->Confirmed[i] += curDelta[1].Confirmed[i];
                state->Confirmed[i] += curDelta[2].Confirmed[i];
                state->Attempt[i] += curDelta[0].Attempt[i];
                state->Attempt[i] += curDelta[1].Attempt[i];
                state->Attempt[i] += curDelta[2].Attempt[i];
            }
        }
    };
} // namespace ClientDetailsProvider
