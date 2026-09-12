#include <client_states.hpp>
#include <file_descriptors_ops.hpp>

namespace naoto::order_gateway
{
    [[nodiscard]] ClientStates make_fd_array(void)
    {
        return ClientStates(FileDescriptorsOps::getMaxFd());
    }
} // namespace naoto::order_gateway
