#pragma once

#include <concepts>
#include <cstdint>

namespace MarketExecution
{

    template <typename T>
    concept HasSequenceId = requires(T t) {
        { t.SequenceId } -> std::same_as<uint32_t>;
    };

    template <typename DeriverEmitter, typename T>
        requires HasSequenceId<T>
    class UdpMulticastEmitter
    {
        void Send(const T *object) noexcept
        {}
    };
} // namespace MarketExecution
