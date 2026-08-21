#include <ClientDetailsProvider.hpp>
#include <ClientState.hpp>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace AccountService;

int main(int argc, char **argv)
{
    std::array<uint8_t, 32> testKey = {
        0x74, 0xc6, 0x89, 0xd7, 0x45, 0x4c, 0x46, 0x6c, 0x30, 0xcb, 0x39,
        0x99, 0x3d, 0xeb, 0xb1, 0x82, 0xb1, 0xc2, 0x5f, 0x58, 0x27, 0xae,
        0x85, 0x86, 0xdd, 0xaa, 0x92, 0xee, 0x7d, 0x94, 0xd1, 0x38
    };

    ClientState<16> client1;
    client1.ClientId = 0;
    client1.Key = testKey;

    for (size_t i = 0; i < 16; ++i)
    {
        client1.AssetId[i] = i;
        client1.Confirmed[i] = 1000000;
        client1.Attempt[i] = 1000;
    }

    client1.Authorized = 0;
    client1.Connected = -1;

    ClientState<16> client2;
    client2.ClientId = 1;
    client2.Key = testKey;

    for (size_t i = 0; i < 16; ++i)
    {
        client2.AssetId[i] = i;
        client2.Confirmed[i] = 999999;
        client2.Attempt[i] = 900;
    }

    client2.Authorized = 0;
    client2.Connected = -1;

    ClientState<16> client3;
    client3.ClientId = 2;
    client3.Key = testKey;

    for (size_t i = 0; i < 16; ++i)
    {
        client3.AssetId[i] = i;
        client3.Confirmed[i] = 200000000;
        client3.Attempt[i] = 500;
    }

    client3.Authorized = 0;
    client3.Connected = -1;

    std::vector<ClientState<16>> clients;

    clients.push_back(client1);
    clients.push_back(client2);
    clients.push_back(client3);

    ClientDetailsProvider<126, 16, 128, 32> test(
        argc, argv, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 8082, 16, 16, 0,
        256, 1024, RTE_IPV4(239, 1, 1, 1), 30001, clients);

    test.StartClientDetailsProvider();
}
