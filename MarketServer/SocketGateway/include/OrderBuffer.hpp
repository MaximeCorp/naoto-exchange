#include <Order.hpp>
#include <cstring>
#include <vector>

namespace Gateways
{
    class OrderBuffer
    {
    private:
        std::vector<char> Buffer;
        size_t BufferSize;

    public:
        OrderBuffer(void)
            : BufferSize(0)
        {
            Buffer.resize(sizeof(Order));
        }

        inline void clearBuffer(void) noexcept
        {
            BufferSize = 0;
        }

        // Size should be modulo sizeof(Order)
        inline void addBytes(Order *ptr, size_t size) noexcept
        {
            std::memcpy(Buffer.data() + BufferSize, ptr, size);
        }
    };
} // namespace Gateways
