#include <ClientStates.hpp>
#include <FileDescriptorsOps.hpp>

namespace Gateways
{
    template <size_t MaxPositions>
    [[nodiscard]] ClientStates<MaxPositions> make_fd_array(void)
    {
        return ClientStates<MaxPositions>(FileDescriptorsOps::getMaxFd());
    }
} // namespace Gateways
