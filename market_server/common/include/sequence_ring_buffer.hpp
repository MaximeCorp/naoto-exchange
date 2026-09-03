#pragma once

#include <array>
#include <concepts.hpp>
#include <cstddef>
#include <cstdint>

namespace naoto
{
    template <typename T>
    concept HasSequenceId = requires(T t) {
        { t.SequenceId } -> std::convertible_to<uint32_t>;
    };

    // idx in buffer is fully determined by seq id modulo Size
    template <typename T, size_t Size>
        requires HasSequenceId<T> && PowerOfTwo<Size>
    class SequenceRingBuffer
    {
    private:
        std::array<T *, Size> Buffer;
        uint32_t Tail; // The next free slot seq id
    public:
        SequenceRingBuffer(void)
            : Tail(0)
        {
            Buffer.fill(nullptr);
        }

        [[nodiscard]] T *GetSlot(size_t sequenceId) const noexcept
        {
            size_t idx = sequenceId & (Size - 1);

            bool validSequenceId =
                Buffer[idx] && sequenceId == Buffer[idx]->SequenceId;

            return validSequenceId ? Buffer[idx] : nullptr;
        }

        [[nodiscard]] bool AddSlot(T *toAdd) noexcept
        {
            uint32_t sequenceId = toAdd->SequenceId;
            size_t idx = sequenceId & (Size - 1);

            if (Tail > Size && sequenceId < Tail - Size)
            {
                return false;
            }

            Buffer[idx] = toAdd;

            if (sequenceId >= Tail)
            {
                Tail = sequenceId + 1;
            }

            return true;
        }
    };
} // namespace naoto