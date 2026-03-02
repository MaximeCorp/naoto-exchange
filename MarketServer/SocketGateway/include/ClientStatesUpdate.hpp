#pragma once

#include <cstdint>

namespace Gateways
{
    enum OperationType
    {
        ADDITION,
        ASSIGNEMENT
    };

    class ClientStatesUpdate
    {
        OperationType Type;
        std::int64_t Value;
    };
} // namespace Gateways
