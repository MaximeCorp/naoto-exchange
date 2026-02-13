#include <SocketGateway.hpp>

using namespace Gateways;

int main(void)
{
    SocketGateway<16> gateway(100, 1000, 8080, 32, 32, 100);

    gateway.StartGateway();
}
