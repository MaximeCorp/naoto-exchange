#pragma once

#include <cstdint>

namespace naoto::account_service
{
    template <typename T>
    struct RoutedMessage
    {
        uint16_t GatewayId;
        T *Message;

        RoutedMessage(void) noexcept
            : GatewayId(0)
            , Message(nullptr)
        {}
    };
} // namespace naoto::account_service
