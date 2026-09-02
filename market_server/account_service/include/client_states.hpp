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

            const ClientState<MaxPositions> &ref = complete == 0
                ? States1[clientId]
                : (complete == 1 ? States2[clientId] : States3[clientId]);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                Deltas[clientId][complete].Confirmed[i] +=
                    clientState.Confirmed[i] - ref.Confirmed[i];
                Deltas[clientId][complete].Attempt[i] +=
                    clientState.Attempt[i] - ref.Attempt[i];
            }

            toChange.ClientId = clientState.ClientId;
            toChange.Key = clientState.Key;
            toChange.AssetId = clientState.AssetId;
            toChange.Authorized = clientState.Authorized;
            toChange.Connected = clientState.Connected;

            uint64_t &curSeqId = complete == 0
                ? SequenceIds[clientId][1]
                : (complete == 1 ? SequenceIds[clientId][2]
                                 : SequenceIds[clientId][0]);

            curSeqId = sequenceId;
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
                ? SequenceIds[clientId][1]
                : (complete == 1 ? SequenceIds[clientId][2]
                                 : SequenceIds[clientId][0]);

            curSeqId = sequenceId;
        }

        void FlushTripleBuffer(const uint32_t clientId) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientId])
                                   .load(std::memory_order_relaxed);

            std::array<ClientDelta<MaxPositions>, 3> &curDelta =
                Deltas[clientId];

            ClientState<MaxPositions> *curState = complete == 0
                ? &States2[clientId]
                : (complete == 1 ? &States3[clientId] : &States1[clientId]);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                curState->Confirmed[i] += curDelta[complete].Confirmed[i];
                curState->Attempt[i] += curDelta[complete].Attempt[i];
            }

            uint64_t curSeqId = complete == 0
                ? SequenceIds[clientId][1]
                : (complete == 1 ? SequenceIds[clientId][2]
                                 : SequenceIds[clientId][0]);

            complete = complete == 2 ? 0 : complete + 1;

            std::atomic_ref(Complete[clientId])
                .store(complete, std::memory_order_release);

            // Assumption: the client details will always contain MaxPositions
            // assets (even if some aren't used)

            uint64_t &newSeqId = complete == 0
                ? SequenceIds[clientId][1]
                : (complete == 1 ? SequenceIds[clientId][2]
                                 : SequenceIds[clientId][0]);

            ClientState<MaxPositions> *newState = complete == 0
                ? &States2[clientId]
                : (complete == 1 ? &States3[clientId] : &States1[clientId]);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                curDelta[complete].Confirmed[i] = 0;
                curDelta[complete].Attempt[i] = 0;
                newState->Confirmed[i] += curDelta[0].Confirmed[i];
                newState->Confirmed[i] += curDelta[1].Confirmed[i];
                newState->Confirmed[i] += curDelta[2].Confirmed[i];
                newState->Attempt[i] += curDelta[0].Attempt[i];
                newState->Attempt[i] += curDelta[1].Attempt[i];
                newState->Attempt[i] += curDelta[2].Attempt[i];
                newState->AssetId[i] = curState->AssetId[i];
            }

            // Same bug, same fix, for the fields that aren't per-asset:
            // ClientId/Key/Authorized/Connected only ever got set once
            // by SetClientState() too. Left unpropagated, Key in
            // particular would eventually make CheckKey() fail for a
            // client that's actually still correctly authenticated,
            // once enough flushes rotated a stale (zeroed) buffer back
            // into view.
            newState->ClientId = curState->ClientId;
            newState->Key = curState->Key;
            newState->Authorized = curState->Authorized;
            newState->Connected = curState->Connected;

            newSeqId = curSeqId;
        }
    };
} // namespace naoto::account_service
