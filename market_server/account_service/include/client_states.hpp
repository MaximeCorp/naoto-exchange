#pragma once

#include <atomic>
#include <client_delta.hpp>
#include <client_state.hpp>
#include <readerwritercircularbuffer.h>
#include <vector>

namespace naoto::account_service
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
        alignas(64) std::vector<std::array<uint64_t, 3>> SequenceIds;

        alignas(64) std::vector<uint8_t> Complete;

    public:
        ClientStates(size_t maxClients)
            : States1(maxClients)
            , States2(maxClients)
            , States3(maxClients)
            , Deltas(maxClients)
            , SequenceIds(maxClients)
            , Complete(maxClients, 0)
        {
            for (size_t i = 0; i < maxClients; ++i)
            {
                for (size_t j = 0; j < 3; ++j)
                {
                    SequenceIds[i][j] = 3334;
                }
            }
        }

        ClientStates(size_t maxClients,
                     std::vector<ClientState<MaxPositions>> &states)
            : States1(maxClients)
            , States2(maxClients)
            , States3(maxClients)
            , Deltas(maxClients)
            , SequenceIds(maxClients)
            , Complete(maxClients, 0)
        {
            for (size_t i = 0; i < maxClients; ++i)
            {
                for (size_t j = 0; j < 3; ++j)
                {
                    SequenceIds[i][j] = 3334;
                }
            }

            for (size_t i = 0; i < states.size(); ++i)
            {
                States1[i] = states[i];
                States2[i] = states[i];
                States3[i] = states[i];
            }
        }

        // Consumer methods
        [[nodiscard]] uint8_t
        GetComplete(const uint32_t clientId) const noexcept
        {
            return std::atomic_ref(Complete[clientId])
                .load(std::memory_order_acquire);
        }

        [[nodiscard]] const ClientState<MaxPositions>
        GetClientState(const uint32_t clientId) const noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientId])
                                   .load(std::memory_order_acquire);
            return complete == 0 ? States1[clientId]
                : complete == 1  ? States2[clientId]
                                 : States3[clientId];
        }

        // Producer methods
        void SetClientState(const ClientState<MaxPositions> &clientState,
                            uint64_t sequenceId) noexcept
        {
            const uint32_t clientId = clientState.ClientId;

            uint8_t complete = std::atomic_ref(Complete[clientId])
                                   .load(std::memory_order_relaxed);

            ClientState<MaxPositions> &toChange = complete == 0
                ? States2[clientId]
                : (complete == 1 ? States3[clientId] : States1[clientId]);
            toChange = clientState;

            uint64_t &curSeqId = complete == 0
                ? SequenceIds[1][clientId]
                : (complete == 1 ? SequenceIds[2][clientId]
                                 : SequenceIds[0][clientId]);

            curSeqId = sequenceId;

            // TODO: Should update deltas if this function is ever used
        }

        void SetClientAssets(const uint32_t clientId, const int64_t confirmed,
                             const int64_t attempt, uint16_t assetId,
                             uint64_t sequenceId) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientId])
                                   .load(std::memory_order_relaxed);

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

            uint64_t &curSeqId = complete == 0
                ? SequenceIds[1][clientId]
                : (complete == 1 ? SequenceIds[2][clientId]
                                 : SequenceIds[0][clientId]);

            curSeqId = sequenceId;
        }

        void FlushTripleBuffer(const uint32_t clientId) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientId])
                                   .load(std::memory_order_relaxed);

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

            uint64_t curSeqId = complete == 0
                ? SequenceIds[1][clientId]
                : (complete == 1 ? SequenceIds[2][clientId]
                                 : SequenceIds[0][clientId]);

            complete = complete == 2 ? 0 : complete + 1;

            std::atomic_ref(Complete[clientId])
                .store(complete, std::memory_order_release);

            // Assumption: the client details will always contain MaxPositions
            // assets (even if some aren't used)

            uint64_t &newSeqId = complete == 0
                ? SequenceIds[1][clientId]
                : (complete == 1 ? SequenceIds[2][clientId]
                                 : SequenceIds[0][clientId]);

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

            newSeqId = curSeqId;
        }
    };
} // namespace naoto::account_service
