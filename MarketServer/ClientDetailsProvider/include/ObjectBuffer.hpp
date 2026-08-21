#pragma once

#include <array>
#include <cstring>

namespace AccountService
{
    template <typename T>
    struct ObjectBuffer
    {
        std::array<uint8_t, sizeof(T)> Buffer;
        size_t BufferSize;

        ObjectBuffer(void)
            : BufferSize(0)
        {}

        void clearBuffer(void) noexcept
        {
            BufferSize = 0;
        }

        // Size should be modulo sizeof(Order)
        void addBytes(void *ptr, size_t size) noexcept
        {
            if (BufferSize + size > sizeof(T)) [[unlikely]]
            {
                // Might have to terminate
                return;
            }

            std::memcpy(Buffer.data() + BufferSize, ptr, size);
            BufferSize += size;
        }

        // Caller's responsability to check buffer size
        void readBytes(void *ptr, size_t size) noexcept
        {
            std::memcpy(ptr, Buffer.data(), size);

            BufferSize -= size;

            std::memmove(Buffer.data(), Buffer.data() + size, BufferSize);
        }
    };
} // namespace AccountService
