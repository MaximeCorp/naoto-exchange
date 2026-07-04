#pragma once

#include <ClientState.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <vector>

namespace ClientDetailsProvider
{
    template <size_t MaxPositions>
    class ClientStates
    {
    private:
        alignas(64) std::vector<ClientState<MaxPosisitions>> States;

    public:
        [[nodiscard]] ClientState<MaxPositions> &
        GetClientState(const uint32_t clientId) noexcept
        {
            return States[clientId];
        }
    };
} // namespace ClientDetailsProvider
