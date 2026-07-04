#pragma once

#include <ClientState.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace Gateways
{
    template <size_t MaxPositions>
    class ClientStates
    {
    private:
        // Should switch to AoS -> Access per client, not per field
        // Id = 0 means slot not used

        alignas(64) std::vector<ClientState<MaxPositions>> States1;
        alignas(64) std::vector<ClientState<MaxPositions>> States2;

        // Fd is connected
        alignas(
            64) std::vector<char> Connected; // Always access with atomic_ref
        // Determine which buffer to read/write (granular double buffer)
        alignas(64) std::vector<char> Complete; // Always access with atomic_ref

    public:
        ClientStates(const size_t size)
        {
            States1.resize(size);
            States2.resize(size);

            std::cout << "Creating a Client States array of size " << size
                      << "\n";
        }

        [[nodiscard]] int32_t get_client_id(const uint32_t fd) noexcept
        {
            return std::atomic_ref<char>(Complete[fd])
                       .load(std::memory_order_acquire)
                ? States1[fd].GetClientId()
                : States2[fd].GetClientId();
        }
        [[nodiscard]] int64_t get_confirmed(const uint32_t fd,
                                            const uint16_t assetId) noexcept
        {
            return std::atomic_ref<char>(Complete[fd])
                       .load(std::memory_order_acquire)
                ? States1[fd].GetAssetConfirmed(assetId)
                : States2[fd].GetAssetConfirmed(assetId);
        }
        [[nodiscard]] int64_t get_attempt(const uint32_t fd,
                                          const uint16_t assetId) noexcept
        {
            return std::atomic_ref<char>(Complete[fd])
                       .load(std::memory_order_acquire)
                ? States1[fd].GetAssetAttempt(assetId)
                : States2[fd].GetAssetAttempt(assetId);
        }
        void add_client(const uint32_t fd,
                        const ClientState<MaxPositions> *clientState) noexcept
        {
            ClientState<MaxPositions> *curClient =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? &States2[fd]
                : &States1[fd];

            std::memcpy(clientState, curClient,
                        sizeof(ClientState<MaxPositions>));

            std::atomic_ref<char>(Connected[fd])
                .store(true, std::memory_order_release);
        }

        void remove_client(const std::uint32_t fd) noexcept
        {
            std::atomic_ref<char>(Connected[fd])
                .store(0, std::memory_order_release);
        }

        [[nodiscard]] bool
        can_spend(const std::uint32_t fd, const std::int64_t amount,
                  const uint16_t assetId) // Not correct yet (needs the addition
                                          // of local attempt for risk service)
        {
            const ClientState<MaxPositions> &curClient =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? States1[fd]
                : States2[fd];

            size_t assetIdx = 0;

            while (curClient.GetAssetIdAt(assetIdx) != assetId
                   && assetIdx < MaxPositions)
            {
                ++assetIdx;
            }

            std::cout << "checking client at fd " << fd << "\n";

            if (assetIdx >= MaxPositions) [[unlikely]]
            {
                return false;
            }

            int64_t curConfirmed = curClient.GetConfirmedAt(assetIdx);
            int64_t curAttempt = curClient.GetAttemptAt(assetIdx);

            if (amount <= 0 || amount > curConfirmed - curAttempt) [[unlikely]]
            {
                return false;
            }

            return true;
        }
    };

    template <size_t MaxPositions>
    [[nodiscard]] ClientStates<MaxPositions> make_fd_array(void);
} // namespace Gateways
