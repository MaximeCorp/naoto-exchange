#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace Gateways
{
    class ClientStates
    {
    private:
        // Id = 0 means slot not used
        alignas(64) std::vector<std::uint32_t> Id;
        // Confirmed amount of money
        alignas(64) std::vector<std::int64_t> Confirmed1;
        alignas(64) std::vector<std::int64_t> Confirmed2;
        // Amount of money taking pending transactions into account
        alignas(64) std::vector<std::int64_t> Attempt1;
        alignas(64) std::vector<std::int64_t> Attempt2;
        // Fd is connected
        alignas(
            64) std::vector<char> Connected; // Always access with atomic_ref
        // Which buffer (granular double buffer)
        alignas(64) std::vector<char> Complete; // Always access with atomic_ref

    public:
        ClientStates(const size_t size)
        {
            Id.resize(size, 0);
            Confirmed1.resize(size, 0);
            Attempt1.resize(size, 0);
            Confirmed2.resize(size, 0);
            Attempt2.resize(size, 0);
            Connected.resize(size, 0);
            Complete.resize(size, 0);

            std::cout << "Creating a Client States array of size " << size
                      << "\n";
        }

        [[nodiscard]] std::uint32_t
        get_client_id(const std::uint32_t fd) noexcept
        {
            return Id[fd];
        }
        [[nodiscard]] std::int64_t
        get_confirmed(const std::uint32_t fd) noexcept
        {
            return std::atomic_ref<char>(Complete[fd])
                       .load(std::memory_order_acquire)
                ? Confirmed1[fd]
                : Confirmed2[fd];
        }
        [[nodiscard]] std::int64_t get_attempt(const std::uint32_t fd) noexcept
        {
            return std::atomic_ref<char>(Complete[fd])
                       .load(std::memory_order_acquire)
                ? Attempt1[fd]
                : Attempt2[fd];
        }
        void add_client(const std::uint32_t fd, const std::uint32_t id,
                        const std::int64_t confirmed) noexcept
        {
            Id[fd] = id;
            std::vector<std::int64_t> &confirmed_buffer =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? Confirmed1
                : Confirmed2;
            std::vector<std::int64_t> &attempt_buffer =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? Attempt1
                : Attempt2;
            confirmed_buffer[fd] = confirmed;
            attempt_buffer[fd] = 0;

            std::atomic_ref<char>(Connected[fd])
                .store(true, std::memory_order_release);
        }

        void add_client(const std::uint32_t fd, const std::uint32_t id,
                        const std::int64_t confirmed,
                        const std::int64_t attempt) noexcept
        {
            Id[fd] = id;
            std::vector<std::int64_t> &confirmed_buffer =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? Confirmed1
                : Confirmed2;
            std::vector<std::int64_t> &attempt_buffer =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? Attempt1
                : Attempt2;
            confirmed_buffer[fd] = confirmed;
            attempt_buffer[fd] = attempt;

            std::atomic_ref<char>(Connected[fd])
                .store(1, std::memory_order_release);
        }

        void remove_client(const std::uint32_t fd) noexcept
        {
            std::atomic_ref<char>(Connected[fd])
                .store(0, std::memory_order_release);
        }

        [[nodiscard]] bool can_spend(
            const std::uint32_t fd,
            const std::int64_t amount) // Not correct yet (needs the addition of
                                       // local attempt for risk service)
        {
            std::vector<std::int64_t> &confirmed_buffer =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? Confirmed1
                : Confirmed2;
            std::vector<std::int64_t> &attempt_buffer =
                std::atomic_ref<char>(Complete[fd])
                    .load(std::memory_order_acquire)
                ? Attempt1
                : Attempt2;
            std::cout << "checking client at fd " << fd << "\n";
            if (amount >= 0
                && amount <= confirmed_buffer[fd] - attempt_buffer[fd])
                [[likely]]
            {
                return true;
            }

            return true;
        }
    };

    [[nodiscard]] ClientStates make_fd_array(void);
} // namespace Gateways
