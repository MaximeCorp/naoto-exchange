#include <client_states.hpp>
#include <file_descriptors_ops.hpp>

namespace naoto::order_gateway
{
    template <size_t MaxPositions>
    [[nodiscard]] ClientStates<MaxPositions> make_fd_array(void)
    {
        return ClientStates<MaxPositions>(FileDescriptorsOps::getMaxFd());
    }
} // namespace naoto::order_gateway
