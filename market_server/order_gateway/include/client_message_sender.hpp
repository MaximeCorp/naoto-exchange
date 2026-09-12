#pragma once

#include <array>
#include <client_message.hpp>
#include <order_gateway_types.hpp>
#include <system_conf.hpp>

namespace naoto::order_gateway
{
    class ClientMessageSender
    {
    private:
        OrderConfirmationConsumer IncomingConfirmation;
        std::array<ConfirmationResendBuffer, GatewayMaxClients>
            ConfirmationBuffer;
        // TODO : Add client state to compare user id

        template <typename T>
        [[nodiscard]] bool DrainBuffer(
            BytesBuffer<GatewayEpollReceiveBatchSize * sizeof(T)> &buffer,
            const uint32_t fd) noexcept
        {
            size_t totalSent = 0;

#ifndef NAOTO_SHARED_MEMORY
            if constexpr (std::is_same_v<T, Order>)
            {
                totalSent = OrderOffset;
            }
#endif

            const uint8_t *data = buffer.GetData();

            const size_t bufferSize = buffer.GetSize();

            while (totalSent < bufferSize)
            {
                ssize_t sent = send(fd, data + totalSent,
                                    bufferSize - totalSent, MSG_NOSIGNAL);

                totalSent += sent;

                if (sent <= 0) [[unlikely]]
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

#ifndef NAOTO_SHARED_MEMORY
                    if constexpr (std::is_same_v<T, Order>)
                    {
                        auto [sentOrders, sentBytes] =
                            std::div((int)(totalSent), (int)sizeof(Order));

                        buffer.Shift(sentOrders * sizeof(Order));
                        OrderOffset = sentBytes;
                    }
#endif
                    if constexpr (std::is_same_v<T, OrderConfirmation>)
                    {
                        buffer.Shift(totalSent);
                    }

                    return false;
                }
            }

            buffer.Clear();

#ifndef NAOTO_SHARED_MEMORY
            if constexpr (std::is_same_v<T, Order>)
            {
                OrderOffset = 0;
            }
#endif

            return true;
        }

        void SendOrderConfirmation(const OrderConfirmation *confirmation,
                                   const uint32_t fd) noexcept
        {
            size_t totalSent = 0;

            while (totalSent < sizeof(OrderConfirmation))
            {
                ssize_t sent =
                    send(fd, (uint8_t *)(confirmation) + totalSent,
                         sizeof(OrderConfirmation) - totalSent, MSG_NOSIGNAL);

                if (sent <= 0) [[unlikely]]
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

                    ConfirmationBuffer[fd].Add(
                        (uint8_t *)(confirmation) + totalSent,
                        sizeof(OrderConfirmation) - totalSent);

                    return;
                }

                totalSent += sent;
            }
        }

    public:
        ClientMessageSender(OrderConfirmationQueue *incomingConfirmation)
            : IncomingConfirmation(incomingConfirmation)
        {}

        void StartLoop(void) noexcept
        {
            ClientMessage message;

            while (true)
            {
                if (IncomingConfirmation.TryPop(message))
                {
                    bool drained = DrainBuffer<OrderConfirmation>(
                        ConfirmationBuffer[message.Fd], message.Fd);

                    if (!drained) [[unlikely]]
                    {
                        if (ConfirmationBuffer[message.Fd].CanAdd(
                                sizeof(OrderConfirmation))) [[unlikely]]
                        {
                            ConfirmationBuffer[message.Fd].Add(
                                (uint8_t *)&message.Confirmation,
                                sizeof(OrderConfirmation));
                        }
                    }
                    else
                    {
                        SendOrderConfirmation(&message.Confirmation,
                                              message.Fd);
                    }
                }
            }
        }
    };
} // namespace naoto::order_gateway