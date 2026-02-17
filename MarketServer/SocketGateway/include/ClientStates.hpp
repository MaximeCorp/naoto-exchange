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
        alignas(64) std::vector<std::int64_t> Confirmed;
        // Amount of money taking pending transactions into account
        alignas(64) std::vector<std::int64_t> Attempt;
        // Fd is connected
        alignas(64) std::vector<std::atomic<bool>> Connected;
        // Which buffer (granular double buffer)
        alignas(64) std::vector<std::atomic<bool>> Complete;

    public:
        ClientStates(size_t size)
        {
            Id.resize(size, 0);
            Confirmed.resize(size, 0);
            Attempt.resize(size, 0);

            std::cout << "Creating a Client States array of size " << size
                      << "\n";
        }

        [[nodiscard]] inline std::uint32_t
        get_client_id(std::uint32_t fd) noexcept
        {
            return Id[fd];
        }
        [[nodiscard]] inline std::int64_t
        get_confirmed(std::uint32_t fd) noexcept
        {
            return Confirmed[fd];
        }
        [[nodiscard]] inline std::int64_t get_attempt(std::uint32_t fd) noexcept
        {
            return Attempt[fd];
        }

        inline void add_client(std::uint32_t fd, std::uint32_t id,
                               std::int64_t confirmed) noexcept
        {
            Id[fd] = id;
            Confirmed[fd] = confirmed;
            Attempt[fd] = confirmed;
        }

        inline void add_client(std::uint32_t fd, std::uint32_t id,
                               std::int64_t confirmed,
                               std::int64_t attempt) noexcept
        {
            Id[fd] = id;
            Confirmed[fd] = confirmed;
            Attempt[fd] = attempt;
        }

        inline void remove_client(std::uint32_t fd) noexcept
        {
            Id[fd] = 0;
        }

        [[nodiscard]] inline bool can_spend(std::uint32_t fd,
                                            std::int64_t amount)
        {
            std::cout << "checking client at fd " << fd << "\n";
            if (amount >= 0 && amount <= Attempt[fd]) [[likely]]
            {
                return true;
            }

            return false;
        }
    };

    [[nodiscard]] ClientStates make_fd_array(void);
} // namespace Gateways
