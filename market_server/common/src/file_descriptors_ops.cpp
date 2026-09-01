#include <file_descriptors_ops.hpp>

#include <cstring>
#include <iostream>
#include <sys/resource.h>

namespace naoto
{
    bool FileDescriptorsOps::getRLimit(struct ::rlimit *rl)
    {
        if (getrlimit(RLIMIT_NOFILE, rl) != 0)
        {
            std::cerr << "Error getting limits: " << strerror(errno)
                      << std::endl;
            return false;
        }

        return true;
    }

    size_t FileDescriptorsOps::findMaxFd(void) noexcept
    {
        struct ::rlimit rl;

        if (!getRLimit(&rl))
        {
            return 0;
        }

        MaxFd = rl.rlim_cur;

        return rl.rlim_cur;
    }

    void FileDescriptorsOps::setMaxFd(const size_t nb_fds)
    {
        struct ::rlimit rl;

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

    void FileDescriptorsOps::capMaxFd(void)
    {
        struct ::rlimit rl;

        if (!getRLimit(&rl))
        {
            return;
        }

        setMaxFd(rl.rlim_max);
    }

    size_t FileDescriptorsOps::getMaxFd(void) noexcept
    {
        return MaxFd.has_value() ? MaxFd.value() : findMaxFd();
    }
} // namespace naoto
