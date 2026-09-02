#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace naoto
{
    template <size_t BufferSize>
    class BytesBuffer
    {
    private:
        std::array<uint8_t, BufferSize> Buffer;
        size_t Size;

    public:
        BytesBuffer(void)
            : Size(0)
        {}

        void Add(const uint8_t *data, const size_t size) noexcept
        {
            std::memcpy(Buffer.data() + Size, data, size);
            // BUG FIX: Size was never advanced here. Every caller
            // (OrderRouter's OrderBuffer/ConfirmationBuffer accumulating
            // wire bytes across possibly-partial socket reads) needs
            // Add() to mean "append `size` bytes and remember they're
            // there" - without this, a second Add() call would silently
            // overwrite the first at the same offset instead of
            // appending after it. Confirmed by
            // tests/market_server/unit/test_bytes_buffer.cpp.
            Size += size;
        }

        void Shift(const size_t size) noexcept
        {
            Size -= size;

            std::memmove(Buffer.data(), Buffer.data() + size, Size);
        }

        void Read(uint8_t *ptr, const size_t size) noexcept
        {
            std::memcpy(ptr, Buffer.data(), size);

            Shift(size);
        }

        void Clear(void) noexcept
        {
            Size = 0;
        }

        [[nodiscard]] bool CanAdd(const size_t size) const noexcept
        {
            return BufferSize - Size >= size;
        }

        [[nodiscard]] uint8_t *GetData(void) noexcept
        {
            return Buffer.data();
        }

        [[nodiscard]] const uint8_t *GetData(void) const noexcept
        {
            return Buffer.data();
        }

        [[nodiscard]] size_t GetSize(void) const noexcept
        {
            return Size;
        }
    };
} // namespace naoto