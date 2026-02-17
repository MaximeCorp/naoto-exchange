#pragma once

#include <ClientStates.hpp>
#include <ClientStatesUpdates.hpp>
#include <FlatHashMap.hpp>
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

namespace Gateways
{
    class ClientStatesInjector
    {
    private:
        ClientStates &ClientInfos;
        ska::flat_hash_map<std::int32_t, std::uint32_t> clientIdToFd;

        int &ClientStatesFd;
        int MarketUpdatesFd;

        int startUdpFd(const std::string &marketUpdatesIp,
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
            local_addr.sin_port = htons(12345);
            bind(MarketUpdatesFd, (struct sockaddr *)&local_addr,
                 sizeof(local_addr));

            struct sockaddr_in local_addr;
            memset(&local_addr, 0, sizeof(local_addr));
            local_addr.sin_family = AF_INET;
            local_addr.sin_addr.s_addr = INADDR_ANY;
            local_addr.sin_port = htons(12345);
            bind(MarketUpdatesFd, (struct sockaddr *)&local_addr,
                 sizeof(local_addr));
        }

    public:
        ClientStatesInjector(ClientStates &clientInfos, int &clientStatesFd)
            : ClientInfos(clientInfos)
            , ClientStatesFd(clientStatesFd)
        {
            startUdpFd("matching engine multicast ip", 8080);
        }

        void startPollingLoop(void) noexcept
        {
            while (true)
            {}
        }
    };
} // namespace Gateways
