#include <account_request_sender.hpp>

#include <iostream>
#include <sys/socket.h>

namespace naoto::order_gateway
{
    void GatewayRequestForwarder::SendRequest(
        RoutedAuthRequest *curRequest) noexcept
    {
        std::cout << "Sending request to account service\n\n";
        uint64_t curVal =
            AccountFd.load(std::memory_order_relaxed); // Relaxed because
                                                   // memory dependancy
                                                   // allows it

        int32_t curFd = VersionedFd::Fd(curVal);

        if (curFd == -1) [[unlikely]]
        {
            std::cerr
                << "Failed fetching user details from account "
                   "service: not connected\n\n";
            return;
        }

        // edge case: if fd gets closed then recycled by and the
        // new fd is for client, then information leak, handle
        // this by closing fd after making sure the sender has
        // seen the new fd

        ssize_t sent = send(curFd, curRequest, sizeof(RoutedAuthRequest),
                           MSG_NOSIGNAL);

        std::cerr << "Sending request to fd " << curFd
                  << ", size=" << sizeof(RoutedAuthRequest)
                  << ", sent=" << sent << "\n";

        // TODO: modify the logic here to call send as many times as
        // necessary, apply same modifications to risk check sendOrder

        if (sent < 0) [[unlikely]]
        {
            if (errno == EPIPE || errno == ECONNRESET)
            {
                // handle order rejection
            }
            // reject order
            return;
        }
        // edge case: send < sizeof(Order)

        uint64_t newVal = AccountFd.load(std::memory_order_acquire);

        if (newVal != curVal) [[unlikely]]
        {
            // Means the send was potentially sent to the wrong
            // fd
            // For later : push to the array / vector of
            // messages to send again
        }
    }

    void GatewayRequestForwarder::Handle(RoutedAuthRequest *curRequest) noexcept
    {
        std::cout << "Account request sender received the request and "
                     "will try "
                     "to send it.\n\n";
        SendRequest(curRequest);
    }

    void AccountRequestSender::StartLoop(void) noexcept
    {
        while (true)
        {
            EpollConsumer.ConsumeTimed(75);
            StatesWriterConsumer.ConsumeTimed(75);
        }
    }
} // namespace naoto::order_gateway
