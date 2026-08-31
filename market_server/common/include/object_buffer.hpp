#pragma once

#include <array>
#include <cstring>

namespace naoto
{
    template <typename T>
    struct ObjectBuffer
    {
        std::array<char, sizeof(T)> Buffer;
        size_t BufferSize;

        ObjectBuffer(void)
            : BufferSize(0)
        {}

        void clearBuffer(void) noexcept
        {
            BufferSize = 0;
        }

        // size should be a multiple of sizeof(T)
        [[nodiscard]] bool addBytes(void *ptr, size_t size) noexcept
        {
            if (BufferSize + size > sizeof(T)) [[unlikely]]
            {
                return false;
            }

            std::memcpy(Buffer.data() + BufferSize, ptr, size);
            BufferSize += size;

            return true;
        }

        // Caller's responsability to check buffer size
        void readBytes(void *ptr, size_t size) noexcept
        {
            std::memcpy(ptr, Buffer.data(), size);

            BufferSize -= size;

            std::memmove(Buffer.data(), Buffer.data() + size, BufferSize);
        }
    };
} // namespace naoto
