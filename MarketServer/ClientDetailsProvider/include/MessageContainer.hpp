#pragma once

#include <cstdint>

namespace AccountService
{
    template <typename T>
    struct MessageContainer
    {
        uint16_t GatewayId;
        T *Message;
    };
} // namespace AccountService
