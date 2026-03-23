#pragma once

#include <Order.hpp>
#include <cstring>
#include <vector>

namespace Gateways
{
    class OrderBuffer
    {
    public:
        std::vector<char> Buffer;
        size_t BufferSize;

        OrderBuffer(void)
            : BufferSize(0)
        {
            Buffer.resize(sizeof(Order));
        }

        void clearBuffer(void) noexcept
        {
            BufferSize = 0;
            std::cout << "cleared\n";
        }

        // Size should be modulo sizeof(Order)
        void addBytes(Order *ptr, size_t size) noexcept
        {
            if (BufferSize + size > sizeof(Order)) [[unlikely]]
            {
                // Might have to terminate
                return;
            }

            std::memcpy(Buffer.data() + BufferSize, ptr, size);
            BufferSize += size;
        }
    };
} // namespace Gateways
