#pragma once

#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <sys/resource.h>

namespace ClientDetailsProvider
{
    // Not thread safe
    class FileDescriptorsOps
    {
    private:
        static inline std::optional<size_t> MaxFd = std::nullopt;

        [[nodiscard]] static bool getRLimit(struct rlimit *rl)
        {
            if (getrlimit(RLIMIT_NOFILE, rl) != 0)
            {
                std::cerr << "Error getting limits: " << strerror(errno)
                          << std::endl;
                return false;
            }

            return true;
        }

        [[nodiscard]] static inline size_t findMaxFd(void) noexcept
        {
            struct rlimit rl;

            if (!getRLimit(&rl))
            {
                return 0;
            }

            MaxFd = rl.rlim_cur;

            return rl.rlim_cur;
        }

    public:
        static inline void setMaxFd(const size_t nb_fds)
        {
            struct rlimit rl;

            if (!getRLimit(&rl))
            {
                return;
            }

            rl.rlim_cur = nb_fds;

            if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
            {
                std::cerr << "Error setting limits: " << strerror(errno)
                          << std::endl;
            }
            else
            {
                std::cout << "Successfully set soft limit to: " << rl.rlim_cur
                          << std::endl;
            }

            MaxFd = nb_fds;
        }

        static inline void capMaxFd(void)
        {
            struct rlimit rl;

            if (!getRLimit(&rl))
            {
                return;
            }

            setMaxFd(rl.rlim_max);
        }

        [[nodiscard]] static inline size_t getMaxFd(void) noexcept
        {
            return MaxFd.has_value() ? MaxFd.value() : findMaxFd();
        }
    };
} // namespace ClientDetailsProvider
