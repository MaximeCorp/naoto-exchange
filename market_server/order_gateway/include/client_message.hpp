#pragma once

#include <cstdint>
#include <order_confirmation.hpp>

namespace naoto::order_gateway
{
    struct ClientMessage
    {
        uint32_t Fd;
        OrderConfirmation Confirmation;

        ClientMessage(void) noexcept
        {}

        explicit ClientMessage(const uint32_t fd,
                               OrderConfirmation confirmation) noexcept
            : Fd(fd)
            , Confirmation(confirmation)
        {}
    };
} // namespace naoto::order_gateway