#pragma once

#include <cstdint>

namespace ClientDetailsProvider
{
    template <typename T>
    struct MessageContainer
    {
        uint16_t GatewayId;
        T *Message;
    };
} // namespace ClientDetailsProvider
