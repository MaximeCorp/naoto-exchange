#pragma once

#include <array>
#include <cstddef>
#include <rte_mbuf.h>
#include <variant>

namespace naoto
{
    // Shared by udp_multicast_emitter.hpp and udp_multicast_receiver.hpp:
    // the burst of mbufs a batching emitter/receiver works on. Unbatched
    // (BatchSize == 0) instantiations carry no buffer at all.
    template <size_t BatchSize, size_t MaxPackets>
    using UdpPacketsBuffer =
        std::conditional_t<(BatchSize > 0), std::array<rte_mbuf *, MaxPackets>,
                           std::monostate>;
} // namespace naoto
