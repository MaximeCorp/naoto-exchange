#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ClientDetailsProvider
{
#pragma pack(push, 1)
    template <size_t MaxPositions>
    struct ClientRequestResponse
    {
        char Status; // 'A' for accepted, 'C' for wrong credentials, 'R' for
                     // refused for other reasons
        uint64_t SequenceId;
        uint32_t ClientId;
        uint32_t ClientFd;
        std::array<uint16_t, MaxPositions> AssetId;
        std::array<int64_t, MaxPositions> Confirmed;
        std::array<int64_t, MaxPositions> Attempt;

        void Clear(void) noexcept
        {
            curResponse->Status = 'R';
            curResponse->SequenceId = 0; // Handle sequence ID
            curResponse->ClientId = clientId;
            curResponse->ClientFd = 0;
            curResponse->AssetId.fill(0);
            curResponse->Confirmed.fill(0);
            curResponse->Attempt.fill(0);
        }
    };
#pragma pack(pop)
} // namespace ClientDetailsProvider
