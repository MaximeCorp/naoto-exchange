#pragma once

#include <ClientStates.hpp>
#include <ClientStatesUpdate.hpp>
#include <ClientStatesUpdates.hpp>
#include <FlatHashMap.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

namespace Gateways
{
    template <size_t MaxPositions>
    class ClientStatesInjector
    {
        using UpdateQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            ClientStatesUpdate *>;

    private:
        ClientStates<MaxPositions> &ClientInfos;
        ska::flat_hash_map<std::int32_t, std::uint32_t> clientIdToFd;

        // UpdateQueue &MarketUpdatesQueue;
        // UpdateQueue &ClientInfoUpdates;

        int ClientStatesFd; // Move to ClientsUpdateInjector
        int MarketUpdatesFd; // Move to MarketUpdatesInjector

        void startUdpFd(const std::string &marketUpdatesIp,
                        const int marketUpdatesPort)
        {
            MarketUpdatesFd = socket(AF_INET, SOCK_DGRAM, 0);

            int reuse = 1;
            setsockopt(MarketUpdatesFd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                       sizeof(reuse));

            struct sockaddr_in local_addr;
            memset(&local_addr, 0, sizeof(local_addr));
            local_addr.sin_family = AF_INET;
            local_addr.sin_addr.s_addr = INADDR_ANY;
            local_addr.sin_port = htons(marketUpdatesPort);
            bind(MarketUpdatesFd, (struct sockaddr *)&local_addr,
                 sizeof(local_addr));

            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = inet_addr(marketUpdatesIp.c_str());
            mreq.imr_interface.s_addr = INADDR_ANY;

            if (setsockopt(MarketUpdatesFd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                           &mreq, sizeof(mreq))
                < 0)
            {
                perror("setsockopt IP_ADD_MEMBERSHIP");
            }
        }

    public:
        ClientStatesInjector(ClientStates<MaxPositions> &clientInfos,
                             const int clientStatesFd,
                             const std::string &MarketUpdatesIp,
                             const int MarketUpdatesPort)
            : ClientInfos(clientInfos)
            , ClientStatesFd(clientStatesFd)
        {
            startUdpFd(MarketUpdatesIp, MarketUpdatesPort);
        }

        void startPollingLoop(void) noexcept
        {
            while (true)
            {}
        }
    };
} // namespace Gateways
