#pragma once

#include <cstdint>
#include <x86intrin.h>

namespace naoto
{
    [[nodiscard]] inline uint64_t capture_start_tsc() noexcept
    {
        __builtin_ia32_lfence();
        return __builtin_ia32_rdtsc();
    }

    [[nodiscard]] inline uint64_t capture_stop_tsc() noexcept
    {
        unsigned int aux;
        const uint64_t tsc = __builtin_ia32_rdtscp(&aux);
        __builtin_ia32_lfence();
        return tsc;
    }

    [[nodiscard]] inline uint64_t now_tsc() noexcept
    {
        __builtin_ia32_lfence();
        uint64_t tsc = __builtin_ia32_rdtsc();
        __builtin_ia32_lfence();
        return tsc;
    }
} // namespace naoto