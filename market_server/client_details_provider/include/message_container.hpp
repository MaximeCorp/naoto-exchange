#pragma once

#include <cstdint>

namespace AccountService
{
    template <typename T>
    struct MessageContainer
    {
        uint16_t GatewayId;
        T *Message;

        MessageContainer(void) noexcept
            : GatewayId(0)
            , Message(nullptr)
        {}
    };
} // namespace AccountService
