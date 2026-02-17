#include <ClientStates.hpp>
#include <FileDescriptorsOps.hpp>

namespace Gateways
{
    [[nodiscard]] ClientStates make_fd_array(void)
    {
        return ClientStates(FileDescriptorsOps::getMaxFd());
    }
} // namespace Gateways
