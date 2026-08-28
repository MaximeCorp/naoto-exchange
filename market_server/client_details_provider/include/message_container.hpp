#pragma once

#include <cstdint>

namespace naoto::client_details_provider
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
} // namespace naoto::client_details_provider
