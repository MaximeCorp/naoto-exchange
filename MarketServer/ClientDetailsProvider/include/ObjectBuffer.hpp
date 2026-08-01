#pragma once

#include <array>
#include <cstring>

namespace AccountService
{
    template <typename T>
    class ObjectBuffer
    {
    public:
        std::array<char, sizeof(T)> Buffer;
        size_t BufferSize;

        ObjectBuffer(void)
            : BufferSize(0)
        {}

        void clearBuffer(void) noexcept
        {
            BufferSize = 0;
            std::cout << "cleared\n";
        }

        // Size should be modulo sizeof(Order)
        void addBytes(T *ptr, size_t size) noexcept
        {
            if (BufferSize + size > sizeof(T)) [[unlikely]]
            {
                // Might have to terminate
                return;
            }

            std::memcpy(Buffer.data() + BufferSize, ptr, size);
            BufferSize += size;
        }
    };
} // namespace AccountService
