#include <account_service.hpp>
#include <client_state.hpp>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace naoto::account_service;

int main(int argc, char **argv)
{
    std::array<uint8_t, 32> testKey = {
        0x74, 0xc6, 0x89, 0xd7, 0x45, 0x4c, 0x46, 0x6c, 0x30, 0xcb, 0x39,
        0x99, 0x3d, 0xeb, 0xb1, 0x82, 0xb1, 0xc2, 0x5f, 0x58, 0x27, 0xae,
        0x85, 0x86, 0xdd, 0xaa, 0x92, 0xee, 0x7d, 0x94, 0xd1, 0x38
    };

    constexpr std::uint32_t NbClients = 12;

    std::vector<ClientState> clients;
    clients.reserve(NbClients);

    for (std::uint32_t id = 0; id < NbClients; ++id)
    {
        ClientState client{};

        client.ClientId = id;
        client.Key = testKey;

        for (size_t i = 0; i < 16; ++i)
        {
            client.AssetId[i] = i;
            client.Confirmed[i] = 10000000000000;
            client.Attempt[i] = 0;
        }

        client.Authorized = 0;
        client.Connected = -1;

        clients.push_back(client);
    }

    AccountService test(argc, argv, 8082, 16, 16, 0, 256, 1024,
                        RTE_IPV4(239, 1, 1, 1), 30001, clients);

    test.StartAccountService();
}