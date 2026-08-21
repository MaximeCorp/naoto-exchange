#include <SocketGateway.hpp>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace Gateways;

int main(int argc, char **argv)
{
    SocketGateway<16, 16, 16, 128, 256, 256> gateway(
        argc, argv, 0, 256, 1024, RTE_IPV4(239, 1, 1, 1), 30001, 128, 512, 8081,
        16, 16);

    gateway.StartGateway();
}
