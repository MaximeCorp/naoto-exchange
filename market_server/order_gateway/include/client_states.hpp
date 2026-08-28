#pragma once

#include <atomic>
#include <client_delta.hpp>
#include <client_request_response.hpp>
#include <client_state.hpp>
#include <readerwritercircularbuffer.h>
#include <vector>

namespace naoto::order_gateway
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

        alignas(64) std::vector<uint8_t> Complete;

    public:
        ClientStates(size_t maxClients)
            : States1(maxClients)
            , States2(maxClients)
            , States3(maxClients)
            , Deltas(maxClients)
            , Complete(maxClients, 0)
        {}

        ClientStates(size_t maxClients,
                     std::vector<ClientState<MaxPositions>> &states)
            : States1(maxClients)
            , States2(maxClients)
            , States3(maxClients)
            , Deltas(maxClients)
            , Complete(maxClients, 0)
        {
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

        [[nodiscard]] uint32_t GetClientId(const uint32_t clientFd) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientFd])
                                   .load(std::memory_order_acquire);
            return complete == 0 ? States1[clientFd].ClientId
                : complete == 1  ? States2[clientFd].ClientId
                                 : States3[clientFd].ClientId;
        }

        [[nodiscard]] ClientState<MaxPositions>
        GetClientState(const uint32_t clientFd) const noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientFd])
                                   .load(std::memory_order_acquire);
            return complete == 0 ? States1[clientFd]
                : complete == 1  ? States2[clientFd]
                                 : States3[clientFd];
        }

        [[nodiscard]] uint8_t GetClientAuth(const uint32_t clientFd) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientFd])
                                   .load(std::memory_order_acquire);
            ClientState<MaxPositions> &curState = complete == 0
                ? States1[clientFd]
                : complete == 1 ? States2[clientFd]
                                : States3[clientFd];

            return curState.Auth;
        }

        [[nodiscard]] bool GetClientFunds(const uint32_t clientFd,
                                          const uint16_t assetId,
                                          int64_t *confirmed,
                                          int64_t *attempt) const noexcept
        {
            ClientState<MaxPositions> curState = GetClientState(clientFd);

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                if (curState.AssetId[i] == assetId)
                {
                    *confirmed = curState.Confirmed[i];
                    *attempt = curState.Attempt[i];

                    return true;
                }
            }

            return false;
        }

        void SetAuthStatus(const uint32_t clientFd, uint8_t auth) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientFd])
                                   .load(std::memory_order_relaxed);

            ClientState<MaxPositions> &state = complete == 0
                ? States2[clientFd]
                : (complete == 1 ? States3[clientFd] : States1[clientFd]);

            state.Auth = !!auth;
        }

        // Producer methods
        void SetClientState(
            const ClientRequestResponse<MaxPositions> *response) noexcept
        {
            const uint32_t clientFd = response->ClientFd;

            uint8_t complete = std::atomic_ref(Complete[clientFd])
                                   .load(std::memory_order_relaxed);

            ClientState<MaxPositions> &ref = complete == 0
                ? States1[clientFd]
                : (complete == 1 ? States2[clientFd] : States3[clientFd]);

            ClientState<MaxPositions> *state = complete == 0
                ? &States2[clientFd]
                : (complete == 1 ? &States3[clientFd] : &States1[clientFd]);

            state->ClientId = response->ClientId;
            state->Auth = 1;
            ++state->SessionId;

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                state->AssetId[i] = response->AssetId[i];
                Deltas[clientFd][complete].Confirmed[i] =
                    response->Confirmed[i] - ref.Confirmed[i];
                Deltas[clientFd][complete].Attempt[i] =
                    response->Attempt[i] - ref.Attempt[i];
            }
        }

        void SetClientAssets(const uint32_t clientId, const int64_t confirmed,
                             const int64_t attempt, uint16_t assetId) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientId])
                                   .load(std::memory_order_relaxed);

            ClientState<MaxPositions> &toChange = complete == 0
                ? States2[clientId]
                : (complete == 1 ? States3[clientId] : States1[clientId]);

            std::cout << "Updating funds of client " << toChange.ClientId
                      << ".\n\n";

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                if (toChange.AssetId[i] == assetId)
                {
                    Deltas[clientId][complete].Confirmed[i] += confirmed;
                    Deltas[clientId][complete].Attempt[i] += attempt;
                }
            }
        }

        void FlushTripleBuffer(const uint32_t clientFd) noexcept
        {
            uint8_t complete = std::atomic_ref(Complete[clientFd])
                                   .load(std::memory_order_relaxed);

            std::array<ClientDelta<MaxPositions>, 3> &curDelta =
                Deltas[clientFd];

            ClientState<MaxPositions> *curState = complete == 0
                ? &States2[clientFd]
                : (complete == 1 ? &States3[clientFd] : &States1[clientFd]);

            const uint32_t clientId = curState->ClientId;
            const uint8_t auth = curState->Auth;
            const uint32_t sessionId = curState->SessionId;

            for (size_t i = 0; i < MaxPositions; ++i)
            {
                curState->Confirmed[i] += curDelta[complete].Confirmed[i];
                curState->Attempt[i] += curDelta[complete].Attempt[i];
            }

            complete = complete == 2 ? 0 : complete + 1;

            std::atomic_ref(Complete[clientFd])
                .store(complete, std::memory_order_release);

            // Assumption: the client details will always contain MaxPositions
            // assets (even if some aren't used)

            ClientState<MaxPositions> *newState = complete == 0
                ? &States2[clientFd]
                : (complete == 1 ? &States3[clientFd] : &States1[clientFd]);

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

            newState->ClientId = clientId;
            newState->Auth = auth;
            newState->SessionId = sessionId;
        }
    };
} // namespace naoto::order_gateway
