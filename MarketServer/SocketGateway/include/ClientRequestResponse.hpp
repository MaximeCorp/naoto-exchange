#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace Gateways
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
            Status = 'R';
            SequenceId = 0; // Handle sequence ID
            ClientId = 0;
            ClientFd = 0;
            AssetId.fill(0);
            Confirmed.fill(0);
            Attempt.fill(0);
        }

        void log() const noexcept
        {
            std::cout << "ClientRequestResponse { Status='" << Status
                      << "', SequenceId=" << SequenceId
                      << ", ClientId=" << ClientId << ", ClientFd=" << ClientFd
                      << " }\n";
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
#pragma pack(pop)
} // namespace Gateways
