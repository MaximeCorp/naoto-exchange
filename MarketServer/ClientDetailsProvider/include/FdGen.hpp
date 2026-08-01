#pragma once

#include <atomic>
#include <cstdlib>

namespace AccountService
{
    class FdGen
    {
    private:
        std::atomic<uint64_t> Packed;

        [[nodiscard]] uint64_t pack(int32_t fd, uint32_t gen) noexcept
        {
            return ((uint64_t)gen << 32) | (uint32_t)fd;
        }

    public:
        FdGen(void)
        {
            Packed.store(pack(-1, 0));
        }

        FdGen(int32_t fd)
        {
            Packed.store(pack(fd, 0));
        }

        void SwitchFd(int32_t fd) noexcept
        {
            uint64_t old = Packed.load(std::memory_order_relaxed);
            uint32_t newgGen = (old >> 32) + 1;
            Packed.store(pack(fd, newgGen), std::memory_order_release);
        }

        [[nodiscard]] uint64_t load(std::memory_order memOrder) noexcept
        {
            return Packed.load(memOrder);
        }

        [[nodiscard]] static int32_t Fd(uint64_t val) noexcept
        {
            int32_t fd = (int32_t)val;
            return fd;
        }

        [[nodiscard]] static uint32_t Gen(uint64_t val) noexcept
        {
            uint32_t gen = val >> 32;
            return gen;
        }
    };
} // namespace AccountService
