#pragma once

#include <bit>
#include <cstddef>

namespace naoto
{
    // Anything indexed with a bitmask (`x & (Size - 1)`) instead of a
    // modulo needs Size to be a power of two, or the mask stops being
    // equivalent to `x % Size` and indexing silently breaks (aliasing,
    // wasted slots, or worse). Shared by FlatHashMap and
    // SequenceRingBuffer, both of which index this way on their hot
    // path specifically to avoid the modulo.
    template <size_t Size>
    concept PowerOfTwo = (Size > 0) && std::has_single_bit(Size);
} // namespace naoto
